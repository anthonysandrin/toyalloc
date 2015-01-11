# (c) 2015 Anthony Sandrin
# This code is licensed under MIT license (see LICENSE.txt for details)

SRCDIR=src
INCDIR=inc
OBJDIR=obj
LIBDIR=lib
LIB=$(LIBDIR)/libtoyalloc.a

SOURCES := $(wildcard $(SRCDIR)/*.c)
OBJECTS := $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

CC=clang -c -o3
CFLAGS=-I $(INCDIR) -I $(SRCDIR)
LD=ar rcs
LDFLAGS=

.PHONY: all
all: $(LIB)

.PHONY: clean
clean:
	rm -rf $(OBJDIR)
	rm -rf $(LIBDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) $< -o $@

$(LIB): $(OBJECTS)
	mkdir -p $(LIBDIR)
	$(LD) $(LIB) $(LDFLAGS) $(OBJECTS)
