1. Overview
    This is a memory allocation libary that implements malloc by using buddy allocation and binary tree.
    The main goal is to reduce lock contention and minimize the amount of memory used for metadata.
    In this memory allocator, each thread has its own thread arena and allocates memory from that arena.
    Thus, each thread can concurrently allocate memory without waiting for other threads to complete their memory allocation.
    A malloc implementation using a double linked list would need to store the address of the previous node and the next node and the size of a memory block.
    If a user program allocates a memory block, it would need 24 ( 8 + 8 + 8) bytes on a 64 bit machine.
    Unlike a double linked list version, this memory allocator only uses 1 byte to store metadata for a memory block.
    4 bits are used to represent the state of a memory block, and the other 4 bits are used to build a binary tree.


2. Future work
    Since each thread allocates memory from its own thread arena, there needs no lock.
    However, there is a case where a thread frees a memory block allocated from another thread's arena.
    Thus, each thread still needs to acquire a lock for its own thread arena although there is no lock contention in most cases. 
    There could be a better way to handle such case.

    Buddy allocation has the internal fragmentation problem. If a thread allocates 3 KB from a 4 KB bin, 1KB is wasted.
    The current implementation only uses 10 diffrent states(4 bits) to represent the state of a memory block. 
    Using more states could minimize the internal fragmentation problem. 


3. System Requirements
    Linux (64 bit)

    
4. Complie
    You can use one of the following commands on a terminal
    $ make
    $ make build
    $ make rebuild


5. Remove object files and executables
    $ make clean


6. Automated Test
    $ make test


7. Manual Test (After compilation)
    $ LD_PRELOAD=./libmalloc.so ./test1

   