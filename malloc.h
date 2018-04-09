#include <stddef.h>

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Allocates size bytes
void* malloc(size_t size); 

// Allocates size bytes. The returned memory address will be a multiple of alignment, which must be a power of two.
void* memalign(size_t alignment, size_t size); 

// Free the memory space pointed to by ptr.
void free(void* ptr); 

// Allocate memory for an array of nmemb elements of size bytes each.
void* calloc(size_t nmemb, size_t size);

// Change the size of the memory block pointed to by ptr to size bytes.
void* realloc(void *ptr, size_t size);

// Print malloc statistics
void malloc_stats(void); 