This project involves implementing a dynamic memory allocator in C, mimicking the memory management behavior of the standard C library. 
The mm_init function handles the necessary initialization, while malloc allocates a block of memory aligned to 16 bytes. 
The free function frees memory, ensuring that the block has been previously allocated and has not been freed. 
The realloc function resizes an existing block of memory, expanding or shrinking it while preserving its contents as much as possible.

In addition, a heap consistency checker, mm_checkheap, must be developed to ensure the integrity of the memory heap. 
This checker will verify invariants, such as validating free blocks, preventing overlapping allocations, and ensuring correct free list pointers, to help debug and maintain a stable allocator.
