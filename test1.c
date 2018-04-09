#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include "malloc.h"

#define MAX_THREAD_NUM 2

// The minimun boundary of memory allocation. (ex) malloc(1) still allocates 8 bytes internally)
#define MIN_MEMORY_ALIGNMENT sizeof(void*)	

// This function is invoked on creation of a new thread
void* ThreadFunc(void* pArg_);
	
// Test malloc()
int MallocTest();

// Test memalign()
int MemAlginTest();

// Test calloc()
int CallocTest();

// Test realloc()
int ReallocTest();

// Test free()
int FreeTest();

// Main Function
int main(int argc, char* argv[])
{
	pthread_t uiThread[MAX_THREAD_NUM];

	// Create new threads
	for (int i = 0; i < MAX_THREAD_NUM; ++i)
	{

		if (0 != pthread_create(&uiThread[i], NULL, ThreadFunc, NULL))
		{
			printf("pthread_create() failed\n");
			return 0;
		}
	}
	
	// Wait for other threads
	void* pReturn[MAX_THREAD_NUM];
	for(int i = 0 ; i < MAX_THREAD_NUM; ++i)
	{
		if (0 != pthread_join(uiThread[i], &pReturn[i]))
		{
			printf("pthread_join() failed\n");
			return -1;
		}
		
		if (NULL == pReturn[i])
		{
			printf("Error in ThreadFunc()\n");
			return -1;
		}
		else
		{
			// Free memory allocated in another thread
			free(pReturn[i]);
		}
		
	}
	
	// The main thread does not allocate any memory explicitly, but GLIBC calls calloc() for each thread's TLS.
	// Thus, the main thread arena has some space in use in the output from malloc_stats() with two allocation requests (two threads)
	// However, used space on other thread arenas must be 0 in the output.
	malloc_stats();
	
	return 0;
}

// This function is invoked on creation of a new thread
// Return NULL on Failure
// Return a valid memory address
void* ThreadFunc(void* pArg_)
{
	if (-1 == MallocTest())
	{
		printf("MallocTest() Failed\n");
		return NULL;
	}
	
	if (-1 == MemAlginTest())
	{
		printf("MemAlginTest() Failed\n");
		return NULL;
	}
	
	if (-1 == CallocTest())
	{
		printf("CallocTest() Failed\n");
		return NULL;
	}
	
	if (-1 == ReallocTest())
	{
		printf("ReallocTest() Failed\n");
		return NULL;
	}
	
	
	if (-1 == FreeTest())
	{
		printf("FreeTest() Failed\n");
		return NULL;
	}
	
	
	unsigned char* pMem = malloc(4);
	return pMem;
	return NULL;
}

// Test malloc()
// Return -1 on Failure
// Return 0 on Success
int MallocTest()
{
	size_t uiBaseSize = MIN_MEMORY_ALIGNMENT;
	
	// This should still allocate 8 bytes internally
	unsigned char* pMem1 = (unsigned char*)malloc(1);
	for (int i = 0; i < 10; ++i)
	{
		// Allocate iBaseSize -1 bytes to see whether the size of the allocated memory is a multiple of sizeof(void*)
		unsigned char* pMem2 = (unsigned char*)malloc(uiBaseSize - 1);
		// Check whether Buddy Allocation works correctly
		if (pMem2 - pMem1 != uiBaseSize)
		{
			printf("Buddy Allocation has a bug\n");
			return -1;
		}
		
		free(pMem2);
		uiBaseSize *= 2;
	}
	
	free(pMem1);
	return 0;
}

// Test memalign()
// Return -1 on Failure
// Return 0 on Success
int MemAlginTest()
{
	size_t uiBaseSize = MIN_MEMORY_ALIGNMENT;

	for (int i = 0; i < 10; ++i)
	{
		unsigned char* pMem1 = (unsigned char*)memalign(uiBaseSize, sizeof(unsigned long int));
		unsigned char* pMem2 = (unsigned char*)memalign(MIN_MEMORY_ALIGNMENT, sizeof(unsigned long int));
		// Check whether Buddy Allocation works correctly
		if (pMem2 - pMem1 != uiBaseSize)
		{
			printf("Buddy Allocation has a bug\n");
			return -1;
		}
		
		free(pMem1);
		free(pMem2);
		uiBaseSize *= 2;
	}
	
	// Test Invalid Input 1
	unsigned char* pMem3 = memalign(MIN_MEMORY_ALIGNMENT + 2, 100);
	if (NULL != pMem3)
	{
		printf("Invalid Input checking in memalign() failed!\n");
		return -1;
	}
	
	// Test Invalid Input 2
	unsigned char* pMem4 = memalign(MIN_MEMORY_ALIGNMENT - 1, 100);
	if (NULL != pMem4)
	{
		printf("memalign() should not allow an alignment value smaller than MIN_MEMORY_ALIGNMENT\n");
		return -1;
	}
		
	return 0;
}

// Test calloc()
// Return -1 on Failure
// Return 0 on Success
int CallocTest()
{
	unsigned long int* pMem1 = (unsigned long int*)malloc(sizeof(unsigned long int) * 2);
	unsigned long int uiTest1 = 12345;
	unsigned long int uiTest2 = 54321;
	*pMem1 = uiTest1;
	*(pMem1 + 1) = uiTest2;
	free(pMem1);
	
	unsigned long int* pMem2 = (unsigned long int*)malloc(sizeof(unsigned long int) * 2);
	// free() should not initialize memory
	if (*pMem2 != uiTest1 || *(pMem2 + 1) != uiTest2 )
	{
		printf("Unexpected result\n");
		return -1;
	}
	
	free(pMem2);
	
	unsigned long int* pMem3 = (unsigned long int*)calloc(2, sizeof(unsigned long int));
	if (pMem1 != pMem3)
	{
		printf("calloc() allocated memory at an unexpected address\n");
		return -1;
	}
	
	if (*pMem3 != 0 || *(pMem3 + 1) != 0)
	{
		printf("calloc() failed to initialize the memory\n");
		return -1;
	}
	
	free(pMem3);
	
	return 0;
}

// Test realloc()
// Return -1 on Failure
// Return 0 on Success
int ReallocTest()
{
	unsigned long int* pMem1 = (unsigned long int*)malloc(sizeof(unsigned long int));
	unsigned long int uiTest = 12345;
	*pMem1 = uiTest;
	unsigned long* pMem2 = realloc(pMem1, 2 * sizeof(unsigned long int));
	// In current implementation, realloc() always return a different address from the original address.
	if (pMem1 == pMem2)
	{
		printf("realloc() failed\n");
		return -1;
	}

	// Data check
	if (*pMem2 != uiTest)
	{
		printf("realloc() failed to copy the data to the new memory\n");
		return -1;
	}
	
	unsigned long* pMem3 = (unsigned long int*)malloc(sizeof(unsigned long int));
	
	// Check whether the orginal memory was freed correctly
	if (pMem1 != pMem3)
	{
		printf("realloc() failed to free the orignal memory\n");
		return -1;
	}
	
	free(pMem2);
	free(pMem3);
	return 0;
}

// Test free()
// Return -1 on Failure
// Return 0 on Success
int FreeTest()
{
	unsigned char* pMem1 = (unsigned char*)malloc(16);
	
	//This should not free any memory
	free(pMem1 + 8);
	
	unsigned char* pMem2 = (unsigned char*)malloc(8);
	if (pMem1 + 16 != pMem2)
	{
		printf("free() does not work correctly\n");
		return -1;
	}
	
	free(pMem1);
	free(pMem2);
	

	unsigned char* pMem3 = (unsigned char*)malloc(32);
	// If memory were free correctly, pMem == pMem3 will be true
	if (pMem1 != pMem3)
	{
		printf("free() does not work correctly\n");
		return -1;
	}
	
	free(pMem3);
	
	return 0;
}
