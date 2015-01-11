/* (c) 2015 Anthony Sandrin
 * This code is licensed under MIT license (see LICENSE.txt for details) */

#include <sys/mman.h>
#include <unistd.h>

#include "toyalloc.h"

#define ZONE_SIZE 0x7FFF

typedef struct BlockHeader
{
    unsigned short size; //The last bit is zero.
}
BlockHeader;

typedef struct FreeBlockHeader
{
    unsigned short size; //The last bit is zero.
    struct FreeBlockHeader *next;
    struct FreeBlockHeader *prev;
}
FreeBlockHeader;

typedef struct FreeBlockTail
{
    unsigned short size; //The last bit is a free flag.
}
FreeBlockTail;

typedef struct BlockTail
{
    unsigned char free; //The last bit is a free flag.
}
BlockTail;

typedef struct ZoneHeader
{
    struct ZoneHeader *next;
    struct ZoneHeader *prev;
    unsigned char free; //Last bit is zero.
}
ZoneHeader;

typedef struct ZoneTail
{
    unsigned short end; //Last bit is one.
}
ZoneTail;

typedef struct LargeHeader
{
    struct LargeHeader *next;
    struct LargeHeader *prev;
    toy_size_t size; //List bit is one.
}
LargeHeader;

char error;

ZoneHeader *free_zones;
int free_zone_count;

ZoneHeader *zones;
int zone_count;

FreeBlockHeader *free_lists[16];

LargeHeader *large_blocks;

ZoneTail *zone_tail(ZoneHeader *zone)
{
    return (ZoneTail*)((char*)zone + ZONE_SIZE - sizeof(ZoneHeader));
}

unsigned short block_size(BlockHeader *block)
{
    return (block->size & 0x00FF) | (((block->size & 0xFF00) >> 9) << 8);
}

void set_block_size(BlockHeader *block, unsigned short size)
{
    block->size = (size & 0x00FF) | (size & 0xFF00 << 1);
}

unsigned short tail_size(FreeBlockTail *tail)
{
    return (tail->size & 0x00FF) | (((tail->size & 0xFF00) >> 9) << 8);
}
void set_tail_size(FreeBlockTail *tail, unsigned short size)
{
    tail->size = (size & 0x00FF) | (size & 0xFF00 << 1);
}

void set_free(FreeBlockTail *tail)
{
    tail->size |= 0x0100;
}

void unset_free(FreeBlockTail *tail)
{
    tail->size &= 0xFEFF;
}

BlockTail *block_tail(BlockHeader *block)
{
    return (BlockTail*)((char*)block
                        + sizeof(BlockHeader)
                        + block_size(block));
}

BlockTail *prev_tail(BlockHeader *block)
{
    return (BlockTail*)((char*)block - sizeof(BlockTail));
}

FreeBlockHeader *block_head(FreeBlockTail *tail)
{
    return (FreeBlockHeader*)((char*)tail
                              + sizeof(FreeBlockTail)
                              - sizeof(BlockTail)
                              - tail_size(tail)
                              - sizeof(BlockHeader));
}

BlockHeader *block_next(BlockHeader *block)
{
    return (BlockHeader*)((char*)block_tail(block) + sizeof(BlockTail));
}

FreeBlockTail *free_tail(BlockTail *tail)
{
    return (FreeBlockTail*)((char*)tail
                            + sizeof(BlockTail)
                            - sizeof(FreeBlockTail));
}

toy_size_t large_size(LargeHeader* large_block)
{
    return (large_block->size & 0x00FFFFFFFFFFFFFF) |
           (((large_block->size & 0xFF00000000000000) >> 9) << 8);
}

void set_large_size(LargeHeader* large_block, toy_size_t size)
{
    large_block->size = (size & 0x00FFFFFFFFFFFFFF) | (size & 0xFF00000000000000 << 1);
}

toy_size_t page_rounded_size(toy_size_t size)
{
    return ((size >> 12) + 1) >> 12;
}

void add_to_free_list(FreeBlockHeader *block)
{
    //Find the smallest possible list.
    char list = 16;
    unsigned short size = block_size((BlockHeader*)block);
    while(!(size >> list) && list > 0) {
        list--;
    }

    if(free_lists[list] == NULL) {
        block->next = block;
        block->prev = block;
    } else {
        block->next = free_lists[list];
        block->prev = free_lists[list]->prev;
        block->next->prev = block;
        block->prev->next = block; //here.
    }
    free_lists[list] = block;
}

ZoneHeader *create_zone()
{
    void *p = mmap(0, ZONE_SIZE, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);
    if(p == MAP_FAILED) {
        error = 1;
        return NULL;
    }
    return (ZoneHeader*)p;
}

void add_free_zone()
{
    ZoneHeader *zone = create_zone();
    if(error) {
        return;
    }

    if(free_zones == NULL) {
        zone->next = zone;
        zone->prev = zone;
        free_zones = zone;
    } else {
        zone->next = free_zones;
        zone->prev = free_zones->prev;
        zone->prev->next = zone;
        zone->next->prev = zone;
        free_zones = zone;
    }

    //Set the last bit to one. This is used to test if we are at the end
    //of a zone when freeing a block.
    zone_tail(zone)->end = 0xFFFF;

    //Create a free block to fill the empty zone.
    FreeBlockHeader *block = (FreeBlockHeader*)((char*)zone
                                                + sizeof(ZoneHeader));

    unsigned short size = (unsigned short)(ZONE_SIZE
                                           - sizeof(ZoneHeader)
                                           - sizeof(ZoneTail)
                                           - sizeof(BlockHeader)
                                           - sizeof(BlockTail));

    set_block_size((BlockHeader*)block, size); 
    set_tail_size(free_tail(block_tail((BlockHeader*)block)), size);
    set_free(free_tail(block_tail((BlockHeader*)block)));

    add_to_free_list(block);
}

BlockHeader *alloc_block(unsigned short size)
{
    //Find the smallest possible list.
    char list = 16;
    while(!(size >> list) && list > 0) {
        list--;
    }

    //Find the smallest non-empty list.
    while(free_lists[list] == NULL && list < 16) {
        list++;
    }

    if(list == 16) {
        //There is no suitable list so create a new zone.
        add_free_zone();
        if(error) {
            return NULL;
        }
        list--;
        while(free_lists[list] == NULL) {
            list--;
        }
    }

    FreeBlockHeader *block = free_lists[list];

    //Remove block from free list.
    if(block->next == block) {
        free_lists[list] = NULL;
    } else {
        block->next->prev = block->prev;
        block->prev->next = block->next;
        free_lists[list] = block->next;
    }

    //If there is enough empty space remaining in the block, fill it with a
    //a new free block.
    int empty_space = block_size((BlockHeader*)block) - size;
    if(empty_space - (int)sizeof(FreeBlockHeader) - (int)sizeof(FreeBlockTail) >= 0) {

        FreeBlockHeader *new_block =
            (FreeBlockHeader*)((char*)block
                               + sizeof(BlockHeader)
                               + size
                               + sizeof(BlockTail));

        set_block_size((BlockHeader*)new_block, empty_space
                                                - sizeof(BlockHeader)
                                                - sizeof(BlockTail));


        //TODO: add some local variables.
        set_tail_size(free_tail(block_tail((BlockHeader*)new_block)), block_size((BlockHeader*)block));
        set_free(free_tail(block_tail((BlockHeader*)new_block)));

        add_to_free_list(new_block);

        set_block_size((BlockHeader*)block, size);
    }

    unset_free(free_tail(block_tail((BlockHeader*)block)));

    return (BlockHeader*)block;
}

void free_block(BlockHeader *block)
{
    //If we are not at the end of the zone and the next block is free.
    if(!(block_next(block)->size & 0x0100) &&
         block_tail(block_next(block))->free & 0x01) {
        FreeBlockHeader *next_block = (FreeBlockHeader*)block_next(block);

        ///Remove the next block from the free list.
        if(next_block->next == next_block) {
            for(int list = 0; list < 16; list++) {
                if(free_lists[list] == next_block) {
                    free_lists[list] = NULL;
                }
            }
        } else {
            next_block->next->prev = next_block->prev;
            next_block->prev->next = next_block->next;
            for(int i = 0; i < 16; i++) {
                if(free_lists[i] == next_block) {
                    free_lists[i] = next_block->next;
                }
            }
        }

        //Join the current block with the next block.
        set_block_size(block, block_size(block)
                              + sizeof(BlockTail)
                              + sizeof(BlockHeader)
                              + block_size((BlockHeader*)next_block));
    }

    //If the previous block is free.
    if(prev_tail(block)->free & 0x01) {
        FreeBlockHeader *prev_block = block_head(free_tail(prev_tail(block)));

        //Remove the previous block from the free list.
        if(prev_block->next == prev_block) {
            for(int list = 0; list < 16; list++) {
                if(free_lists[list] == prev_block) {
                    free_lists[list] = NULL;
                }
            }
        } else {
            prev_block->next->prev = prev_block->prev;
            prev_block->prev->next = prev_block->next;
            for(int i = 0; i < 16; i++) {
                if(free_lists[i] == prev_block) {
                    free_lists[i] = prev_block->next;
                }
            }
        }

        //Join the previous block with the current block.
        set_block_size((BlockHeader*)prev_block, block_size((BlockHeader*)prev_block)
                                                 + sizeof(BlockTail)
                                                 + sizeof(BlockHeader)
                                                 + block_size(block));

        block = (BlockHeader*)prev_block;
    }

    set_tail_size(free_tail(block_tail(block)), block_size(block));
    set_free(free_tail(block_tail(block)));

    add_to_free_list((FreeBlockHeader*)block);
}

LargeHeader *alloc_large(toy_size_t size)
{
    void *p = mmap(0, page_rounded_size(size + sizeof(LargeHeader)),
                                        PROT_READ|PROT_WRITE,
                                        MAP_ANON|MAP_SHARED,
                                        -1,
                                        0);
    if(p == MAP_FAILED) {
        error = 1;
        return NULL;
    }

    //Insert into the list of large blocks.
    LargeHeader* large_block = (LargeHeader*)p;
    if(large_blocks == NULL) {
        large_blocks = large_block;
        large_block->next = large_block;
        large_block->prev = large_block;
    } else {
        large_block->next = large_blocks;
        large_block->prev = large_blocks->prev;
        large_block->next->prev = large_block;
        large_block->prev->next = large_block;
        large_blocks = large_block;
    }

    set_large_size(large_block, size);
    large_block->size |= 0x0100000000000000;

    return large_block;
}

void free_large(LargeHeader *large_block)
{
    if(large_block->next == large_block) {
        large_blocks = NULL;
    } else {
        large_blocks->next->prev = large_blocks->prev;
        large_blocks->prev->next = large_blocks->next;
        if(large_blocks == large_block) {
            large_blocks = large_block->next;
        }
    }

    munmap(large_block, page_rounded_size(large_size(large_block)
                                          + sizeof(LargeHeader)));
}

void *toy_malloc(toy_size_t size)
{
    error = 0;
    if(size <= 0) {
        return NULL;
    }
    if(size <= sizeof(FreeBlockHeader) + sizeof(FreeBlockTail) - sizeof(BlockHeader) - sizeof(BlockTail)) {
        size = sizeof(FreeBlockHeader) + sizeof(FreeBlockTail) - sizeof(BlockHeader) - sizeof(BlockTail);
    }
    void *p;
    if(size > ZONE_SIZE) {
        p = (char*)alloc_large(size) + sizeof(LargeHeader); 
    } else {
        p = (char*)alloc_block(size) + sizeof(BlockHeader);
    }
    if(error) {
        return NULL;
    }
    return p;
}

void *toy_calloc(toy_size_t nmemb, toy_size_t size)
{
    char *p = toy_malloc(nmemb * size);
    for(int i = 0; i < nmemb * size; i++) {
        *(p + i) = 0x00;
    }
    return p;
}

void toy_free(void *ptr)
{
    if(*((char*)ptr - 1) & 0x01) {
        free_large((LargeHeader*)((char*)ptr - sizeof(LargeHeader)));
    } else {
        free_block((BlockHeader*)((char*)ptr - sizeof(BlockHeader)));
    }
}

void *toy_realloc(void *ptr, toy_size_t size)
{
    if(ptr == NULL) {
        return toy_malloc(size);
    }
    if(size == 0) {
        toy_free(ptr);
        return NULL;
    }

    toy_size_t old_size;
    if(*((char*)ptr - 1) & 0x01) {
        old_size = large_size((LargeHeader*)((char*)ptr - sizeof(LargeHeader)));
    } else {
        old_size = block_size((BlockHeader*)((char*)ptr - sizeof(BlockHeader)));
    }

    if(old_size < size) {
        void *p = toy_malloc(size);

        for(int i = 0; i < old_size; i++) {
            *((char*)p + i) = *((char*)ptr + i);
        }

        toy_free(ptr);

        return p;
    }

    if(old_size / size > 2) {
        void *p = toy_malloc(size);

        for(int i = 0; i < size; i++) {
            *((char*)p + i) = *((char*)ptr + i);
        }

        toy_free(ptr);

        return p;
    }

    return ptr;
}
