Toy Alloc
=========

A simple memory allocator for Unix operating systems written in C.

How it works
------------

Allocations are divided into two groups: Small and Large.

Large allocations are allocated and freed directly.

Small allocations are allocated by allocating large Zones and then dividing
them into small blocks as required.
