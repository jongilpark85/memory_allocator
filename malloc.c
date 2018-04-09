#include "malloc.h"
#include "core.h"
#include <string.h>

// Allocates size bytes
void* malloc(size_t size)
{
	return AllocateMemory(MIN_MEMORY_ALIGNMENT, size);
}


// Free the memory space pointed to by ptr.
void free(void* ptr)
{
	FreeMemory(ptr);
}

// Change the size of the memory block pointed to by ptr to size bytes.
void* realloc(void *ptr, size_t size)
{
	if (NULL == ptr)
		return malloc(size);
	else if (0 == size)
	{
		free(ptr);
		return NULL;
	}
	else
	{
		void* pNewAddr = malloc(size);
		if (pNewAddr)
		{	
			memcpy(pNewAddr, ptr, size);
			free(ptr);
		}
		else
		{
			// If realloc() fails, the original block is left untouched; it is not freed or moved.
			pNewAddr = ptr;
		}

		return pNewAddr;
	}
}

// Allocate memory for an array of nmemb elements of size bytes each.
void* calloc(size_t nmemb, size_t size)
{
	void* pAddr = malloc(nmemb * size);
	if (pAddr)
		memset(pAddr, 0, nmemb * size);
	
	return pAddr;
}

// Allocates size bytes. The returned memory address will be a multiple of alignment, which must be a power of two.
void* memalign(size_t alignment, size_t size)
{
	if (alignment < MIN_MEMORY_ALIGNMENT ||
		(alignment & (alignment - 1)))
		return NULL;
	
	return AllocateMemory(alignment, size);
}

// Print malloc statistics
void malloc_stats(void)
{
	MallocStats();
}