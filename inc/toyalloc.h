/* (c) 2015 Anthony Sandrin
 * This code is licensed under MIT license (see LICENSE.txt for details) */

#ifndef TOYALLOC_H
#define TOYALLOC_H

typedef unsigned long toy_size_t;

void *toy_malloc(toy_size_t size);
void *toy_calloc(toy_size_t nmemb, toy_size_t size);
void toy_free(void *ptr);
void *toy_realloc(void *ptr, toy_size_t size);

#endif
