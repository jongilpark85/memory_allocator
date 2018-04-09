#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <pthread.h>
#include "core.h"

// Global Variables
sem_t g_semProcessLock; // Lock to access resrouce that all threads share
long int g_iPageSize = 0; // Page Size
unsigned char* g_pProcessMetaData = NULL;  // The address of the first page of Process MetaData
unsigned long int g_uiProcessMetaDataPageCounts = 0;  // The number of Process MetaData pages (How many pages are used to store Process MetaData)
unsigned long int g_uiRegisteredThreadCounts = 0; // The number of thread Arenas that have been created (Each thread has its own Arena)

// Offsets to each array in a Process Metadata page
unsigned long int g_uiThreadList_Offset; // offset to the array of Thread IDs .
unsigned long int g_uiThreadMetaList_Offset; // offset to the array of addresses where each Thread Arena Metadata are stored.
unsigned long int g_uiThreadLockList_Offset; // offset to the array of the locks each thread uses.

// The number of per-ThreadData ( ID, Lock, Address of Arena Metadata) can ben stroed in one Process Metadata Page
// The number is cacluated in the constructor of this library
unsigned long int g_uiMaxThreadNums; 

// If one bin uses 2 pages, then the memory space to store MetaData for that bin is 2 * g_uiMetaDataUnitSize
unsigned long int g_uiMetaDataUnitSize; // The size of metadata for one Bin that consists of one page.
// The offsets are always same on each Thread Arena and calcuated in the Constructor of this library
unsigned long int g_uiOffset[TMO_MAX];

// The number of per-BinData ( Bin address, Address of Bin Metadata, the Number of pages, etc) can ben stroed in one Thread Arena Metadata Page
unsigned long int g_uiMaxBinNums;

// Offset to where the size of Thread Arena is stored
unsigned long int g_uiArenaSize_Offset;

// Offset to where the number of Bins is stored 
unsigned long int g_uiBinNums_Offset;


// Thread Local Storage variables ( to access its own Thread Arena Metadata )
__thread unsigned char* t_pThreadMetaData = NULL; // The address of the first page of Thread MetaData
__thread unsigned long int t_uiBinNums = 0; // The number of Bins that have been created
__thread unsigned long int t_uiThreadMeataDataPageCounts = 0; // The number of pages used to store Thread Arena MetaData
__thread unsigned long int t_uiBinMetaNums = 0; // The number of Bin MetaData

//For malloc_stats()
__thread unsigned long int* t_pBinNums = NULL; //  The number of Bins that have been created on this Thread Arena
__thread unsigned long int* t_pArenaSize = NULL; // The size of this Thread Arena, which is the sum of the size of Bins on this Arena

 
// Each thread allocates and frees memory from its own arena, so in most cases, there needs no lock.
// However, a thread can still free memory allocated by another thread, so that is why t_pThreadLock is needed.
__thread sem_t* t_pThreadLock; // 

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Constructor ( before main)
__attribute__((constructor))
void myconstructor() 
{
	sem_init(&g_semProcessLock, 1, 1);
	t_pThreadLock = NULL;
	
	// Get the page size the system uses
	g_iPageSize = sysconf(_SC_PAGESIZE);
	if (-1 == g_iPageSize)
		g_iPageSize = DEFAULT_PAGE_SIZE;
	
	// Set up offsets for Process Metadata
	unsigned long int uiTypeSize = sizeof(unsigned long int);
	unsigned long int uiHeaderLength = uiTypeSize * 2; // Current Address + Next Address
	unsigned long int uiEntrySize = uiHeaderLength + sizeof(sem_t);
	
	g_uiMaxThreadNums = (g_iPageSize - uiHeaderLength) / uiEntrySize;
	g_uiThreadList_Offset = uiHeaderLength;
	g_uiThreadMetaList_Offset = g_uiThreadList_Offset + (uiTypeSize *g_uiMaxThreadNums);
	g_uiThreadLockList_Offset = g_uiThreadMetaList_Offset + (uiTypeSize *g_uiMaxThreadNums);
	
	// Set up offsets for Thread Arena Metadata
	g_uiArenaSize_Offset = uiHeaderLength;
	g_uiBinNums_Offset = g_uiArenaSize_Offset + uiTypeSize;
	uiEntrySize = uiTypeSize * TMO_MAX;
	g_uiMaxBinNums = (g_iPageSize - (uiHeaderLength * 2))  / uiEntrySize;

	g_uiOffset[TMO_BIN] = g_uiBinNums_Offset + uiTypeSize;
	
	unsigned long int uiNewOffset = uiTypeSize * g_uiMaxBinNums;
	for (int i = 1; i < TMO_MAX; ++i)
	{
		g_uiOffset[i] = g_uiOffset[i - 1] + uiNewOffset;
	}

	// 4 bits are used to store the state of each block.
	// If page size is 4096 bytes, and the size of the smallest block is 8 bytes, then, a page consists of 512 (4096/ 8) Blocks.
	// If a Bin uses a single page, the memory space needed to store the state of each block is 256 ( 512 /2) Bytes.
	// However, this library uses a Binary tree,so two buddy blocks need to have their imaginary parent block.
	// Two imaginary parent blocks also need to have their imaginary parent block.
	// In this way, there needs 256 * 2 blocks, so actually, 512 bytes are used to store Metadata for a Bin that uses a single page.
	g_uiMetaDataUnitSize = g_iPageSize / MIN_BLOCK_SIZE; // in Byte
	
	CreateNewProcessMetaPage(NULL);
}

// Print Malloc Statistics of each Arena
void MallocStatsThreadArena(unsigned char* pThreadMetaData_)
{
	if (NULL == pThreadMetaData_)
		return;
		
	unsigned char* pCurrentMeta = pThreadMetaData_;
	unsigned long int uiArenaSize = *(unsigned long int*)(pCurrentMeta + g_uiArenaSize_Offset);
	unsigned long int uiTotalBins = *(unsigned long int*)(pCurrentMeta + g_uiBinNums_Offset);
	
	fprintf(stderr, "Total Size : %lu\n", uiArenaSize);
	fprintf(stderr, "Number of Bins : %lu\n", uiTotalBins);
	
	if (0 == uiTotalBins || 0 == uiArenaSize)
		return;
	
	unsigned long int* pBinList = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN]);
	unsigned long int* pBinPageNumList = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_PAGE_NUM]);
	unsigned long int* pBinUsedBytes = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_USED_BYTES]);
	unsigned long int* pBinAllocReqs = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_ALLOC_REQUESTS]);
	unsigned long int* pBinFreeReqs = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_FREE_REQUESTS]);
	unsigned long int uiBinIndex = 0;
	unsigned long int uiActualBinIndex = 0;
	
	while (uiActualBinIndex < uiTotalBins)
	{
		if (uiBinIndex >= g_uiMaxBinNums)
		{
			uiBinIndex = 0;
			pCurrentMeta = (unsigned char*)*(((unsigned long int*)pCurrentMeta) + 1);
			if (NULL == pCurrentMeta)
				return;
			
			pBinList = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN]);
			pBinPageNumList = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_PAGE_NUM]);
			pBinUsedBytes = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_USED_BYTES]);
			pBinAllocReqs = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_ALLOC_REQUESTS]);
			pBinFreeReqs = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_FREE_REQUESTS]);
			
		}
			
		if (0 == pBinList[uiBinIndex])
			return;
		
		fprintf(stderr, "Bin %lu Info\n", uiActualBinIndex);
		fprintf(stderr, "Total Size : %lu\n", pBinPageNumList[uiBinIndex] * g_iPageSize);
		fprintf(stderr, "Used Space : %lu\n", pBinUsedBytes[uiBinIndex]);
		fprintf(stderr, "Free Space : %lu\n", (pBinPageNumList[uiBinIndex] * g_iPageSize) - pBinUsedBytes[uiBinIndex]);
		fprintf(stderr, "Total Allocation Requests : %lu\n", pBinAllocReqs[uiBinIndex]);
		fprintf(stderr, "Total Free Requests : %lu\n", pBinFreeReqs[uiBinIndex]);
		fprintf(stderr, "===========================================\n");
		
		++uiBinIndex;
		++uiActualBinIndex;
	}	
	
	return;
}

// Allocates uiSize_ bytes. The returned memory address will be a multiple of uiAlignment_, which must be a power of two.
void* AllocateMemory(size_t uiAlignment_, size_t uiSize_)
{
	void* pAllocated = NULL;
	// If this is the first time to functions of this library in this thread, create a new Arena for this thread.
	if (NULL == t_pThreadMetaData && NULL == CreateNewThreadArena())
		return NULL;	
		
	sem_wait(t_pThreadLock);
	pAllocated = MallocFromThreadArena(uiSize_, uiAlignment_);
	sem_post(t_pThreadLock);
	
	return pAllocated;
}


// Free the memory space pointed to by ptr.
void FreeMemory(void* ptr)
{
	if (NULL == ptr)
		return;
	
	// If this is the first time to functions of this library in this thread, create a new Arena for this thread.
	if (NULL == t_pThreadMetaData && NULL == CreateNewThreadArena())
		return;	
	
	sem_wait(t_pThreadLock);
	unsigned long int uiResult = FreeFromThreadArena(ptr, t_pThreadMetaData);
	sem_post(t_pThreadLock);
	
	if (ULONG_MAX == uiResult)
		FreeFromAllArenas(ptr);
	
	return;
}

// Print malloc statistics
void MallocStats()
{
	if (NULL == g_pProcessMetaData)
		return;
	
	// Process Metadata are accessed with read-only operations here, so this function does not acquire a lock for Process Metadata.
	// As mentioned in FreeFromAllThreadArenas(), it is possible that when this function loads g_uiRegisteredThreadCounts, 
	// other information of the new Thread Arena may have not been updated to memory yet ( Because of Processor Memory ordering and differnt cache lines)
	// If the old value of g_uiRegisteredThreadCounts was loaded, then there is no issue.
	// Even if the new value of g_uiRegisteredThreadCounts was loaded, 
	// it is likely that other information of the new Thread Arena will have been updated to memory by the time, this function starts searching on that new Thread Arena.
	// This is beacuse this function loads g_uiRegisteredThreadCounts at the begining and searchs all the Thread Arena starting from the oldest one.
	// In addition, this function does not search on the new Thread Arena if any information of the new Thread Arena contains old values.
	
	unsigned long int uiRegisteredThreadCounts = g_uiRegisteredThreadCounts;
	unsigned char* pCurrentMeta = g_pProcessMetaData;
	pthread_t* pThreadList = (pthread_t*)(pCurrentMeta + g_uiThreadList_Offset);
	unsigned long* pThreadMetaList = (unsigned long int*)(pCurrentMeta + g_uiThreadMetaList_Offset);
	sem_t* pThreadLockList = (sem_t*)(pCurrentMeta + g_uiThreadLockList_Offset);
	unsigned long int uiThreadIndex = 0;
	unsigned long int uiActualThreadIndex = 0;

	
	while (uiActualThreadIndex < uiRegisteredThreadCounts)
	{
		if (uiThreadIndex >= g_uiMaxThreadNums)
		{
			uiThreadIndex = 0;
			pCurrentMeta = (unsigned char*)*(((unsigned long int*)pCurrentMeta) + 1);
			if (NULL == pCurrentMeta)
				return;
			
			pThreadList = (pthread_t*)(pCurrentMeta + g_uiThreadList_Offset);
			pThreadMetaList = (unsigned long int*)(pCurrentMeta + g_uiThreadMetaList_Offset);
			pThreadLockList = (sem_t*)(pCurrentMeta + g_uiThreadLockList_Offset);
		}
		
		// Old value Checking
		if (pthread_equal(0, pThreadList[uiThreadIndex]))
			continue;
		
		// Old value Checking
		if (-1 == sem_wait(pThreadLockList + uiThreadIndex))
			continue;
		
		fprintf(stderr, "===========================================\n");
		fprintf(stderr, "Arena %lu Info\n", uiActualThreadIndex);
		
		MallocStatsThreadArena((unsigned char*)(pThreadMetaList[uiThreadIndex]));
		sem_post(pThreadLockList + uiThreadIndex);

		++uiThreadIndex;
		++uiActualThreadIndex;
	}
	
	return;
}

// Create a new metadata page for the Thread Arena
// If pNew_ are provided, then use the address as a new metata page.
unsigned char* CreateNewProcessMetaPage(unsigned char* pNew_)
{
	unsigned char* pNewAddr = pNew_;
	if (NULL == pNewAddr)
	{
		pNewAddr = (unsigned char*)mmap(NULL, g_iPageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if ((void *)(-1) == pNewAddr)
		{
			errno = ENOMEM;		
			return NULL;
		}
	}
	
	*(unsigned long int*)pNewAddr = (unsigned long int)pNewAddr;
	//*(((unsigned long int*)(pNewAddr)) + 1) = 0;
	
	unsigned char* pLastMeta = GetLastProcessMetaPage();
	// If this is the first page of the ProcessMetadata
	if (NULL == pLastMeta)
		g_pProcessMetaData = pNewAddr;
	else // Otherwise, add the newly created page to the end of the list
		*(((unsigned long int*)(pLastMeta)) + 1) = (unsigned long int)pNewAddr;

	++g_uiProcessMetaDataPageCounts;
	; 
	
	return pNewAddr;
}


// Create a new metadata page for the Thread Arena
// If pNew_ are provided, then use the address as a new metata page.
unsigned char* CreateNewThreadMeta(unsigned char* pNew_)
{
	unsigned char* pNewAddr = pNew_;
	if (NULL == pNewAddr)
	{
		pNewAddr = (unsigned char*)mmap(NULL, g_iPageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if ((void *)(-1) == pNewAddr)
		{
			errno = ENOMEM;		
			return NULL;
		}
	}
	
	*(unsigned long int*)pNewAddr = (unsigned long int)pNewAddr;

	unsigned char* pLastMeta = GetLastThreadMetaPage();
	// If this is the first page of the ThreadMeta
	if (NULL == pLastMeta)
	{
		t_pThreadMetaData = pNewAddr;
		t_pArenaSize = (unsigned long int*)(t_pThreadMetaData + g_uiArenaSize_Offset);
		t_pBinNums = (unsigned long int*)(t_pThreadMetaData + g_uiBinNums_Offset);
	}
	// Otherwise, add the newly created page to the end of the list
	else
		*(((unsigned long int*)(pLastMeta)) + 1) = (unsigned long int)pNewAddr;

	++t_uiThreadMeataDataPageCounts; 
	
	return pNewAddr;
}

// Create a new Bin and Meta for that bin
unsigned char* CreateNewBin(unsigned long int uiPageNums_, unsigned long int uiMetaPagesNums_)
{
	unsigned long int uiMetadataSize = g_uiMetaDataUnitSize * uiPageNums_;
	unsigned long int uiMetaPageIndex = t_uiBinNums  / g_uiMaxBinNums;
	
	unsigned long int uiPageNeeded = uiPageNums_;
	if (uiMetaPageIndex >= t_uiThreadMeataDataPageCounts)
		++uiPageNeeded;
	
	unsigned char* pBinMeta = GetLargeBinMetaPage(uiMetadataSize);
	if (NULL == pBinMeta)
		uiPageNeeded += uiMetaPagesNums_;

	unsigned char* pNewAddr = (unsigned char*)mmap(NULL, g_iPageSize * uiPageNeeded, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	
	if ((void *)(-1) == pNewAddr)
	{
		errno = ENOMEM;		
		return NULL;
	}
	
	if (uiMetaPageIndex >= t_uiThreadMeataDataPageCounts)
	{
		CreateNewThreadMeta(pNewAddr);
		pNewAddr += g_iPageSize;
	}
	
	unsigned char* pBin = pNewAddr;
	unsigned long int uiNewBinIndex =  t_uiBinNums  % g_uiMaxBinNums;
	unsigned char* pCurrentThreadMeta = GetLastThreadMetaPage();
	
	
	if (NULL == pBinMeta)
	{
		pBinMeta = pBin + (g_iPageSize * uiPageNums_);
		unsigned long int uiPageIndex = t_uiBinMetaNums / g_uiMaxBinNums;
		unsigned long int uiBinMetaIndex = t_uiBinMetaNums % g_uiMaxBinNums;
		
		unsigned char* pThreadMeta = GetThreadMetaPage(uiPageIndex);
		unsigned long int* pBinMetaWholeList = (unsigned long int*)(pThreadMeta + g_uiOffset[TMO_BIN_META_POOL]);
		unsigned long int* pBinMetaPageNumList = (unsigned long int*)(pThreadMeta + g_uiOffset[TMO_BIN_META_PAGE_NUM]);
		unsigned long int* pBinMetaOffsetList = (unsigned long int*)(pThreadMeta + g_uiOffset[TMO_BIN_META_OFFSET]);
		pBinMetaWholeList[uiBinMetaIndex] = (unsigned long int)pBinMeta;
		pBinMetaPageNumList[uiBinMetaIndex] = uiMetaPagesNums_;
		pBinMetaOffsetList[uiBinMetaIndex] = uiMetadataSize;
		
		++t_uiBinMetaNums;	
	}
		

	unsigned long int* pBinList = (unsigned long int*)(pCurrentThreadMeta + g_uiOffset[TMO_BIN]);
	unsigned long int* pBinMetaList = (unsigned long int*)(pCurrentThreadMeta + g_uiOffset[TMO_BIN_META]);
	unsigned long int* pBinPageNumList = (unsigned long int*)(pCurrentThreadMeta + g_uiOffset[TMO_BIN_PAGE_NUM]);
	

	pBinList[uiNewBinIndex] = (unsigned long int)pBin;
	pBinPageNumList[uiNewBinIndex] = uiPageNums_;
	pBinMetaList[uiNewBinIndex] = (unsigned long int)pBinMeta;

	//memset(pBinMeta, 0, uiMetadataSize);
		
	++t_uiBinNums;
	*t_pBinNums = t_uiBinNums;
	*t_pArenaSize += (uiPageNums_ * g_iPageSize);
	
	return pBin;
}


// Memory allocation is only managed with its own Arena
// Allocate memory from its own Arena
void* MallocFromThreadArena(size_t uiSize_, unsigned long int uiMinBlackSize_)
{
	if (0 == uiSize_)
		return NULL;
	
	if (uiSize_ < uiMinBlackSize_)
		uiSize_ = uiMinBlackSize_;
	
	unsigned long int uiMinPageNums = MIN_NEW_PAGE_NUMS;
	unsigned long int uiPageNums = 1;
	while (uiSize_ > g_iPageSize * uiPageNums)
		uiPageNums *= 2;
	
	if (uiPageNums < uiMinPageNums)
		uiPageNums = uiMinPageNums;
	
	unsigned long int uiMetadataSize = g_uiMetaDataUnitSize * uiPageNums;
	unsigned long int uiMetaPageNums = 1;
	while (uiMetadataSize > g_iPageSize * uiMetaPageNums)
		++uiMetaPageNums;
	
	
	unsigned char* pCurrentThreadMetaData = t_pThreadMetaData;
	unsigned long int* pBinList = (unsigned long int*)(pCurrentThreadMetaData + g_uiOffset[TMO_BIN]);
	unsigned long int* pBinPageNumList = (unsigned long int*)(pCurrentThreadMetaData + g_uiOffset[TMO_BIN_PAGE_NUM]);
	unsigned long int* pBinMetaList = (unsigned long int*)(pCurrentThreadMetaData + g_uiOffset[TMO_BIN_META]);
	unsigned long int* pBinUsedBtyes = (unsigned long int*)(pCurrentThreadMetaData + g_uiOffset[TMO_BIN_USED_BYTES]);
	unsigned long int* pBinAllocReqs = (unsigned long int*)(pCurrentThreadMetaData + g_uiOffset[TMO_ALLOC_REQUESTS]);	
	unsigned long int uiBinIndex = 0;
	unsigned long int uiActualBinIndex = 0;
	do
	{
		if (uiBinIndex >= g_uiMaxBinNums)
		{
			uiBinIndex = 0;
			pCurrentThreadMetaData = (unsigned char*)*(((unsigned long int*)pCurrentThreadMetaData) + 1);
			if (NULL == pCurrentThreadMetaData)
			{
				CreateNewBin(uiPageNums, uiMetaPageNums);
				pCurrentThreadMetaData = GetLastThreadMetaPage();
			}
				
			pBinList = (unsigned long int*)(pCurrentThreadMetaData + g_uiOffset[TMO_BIN]);
			pBinPageNumList = (unsigned long int*)(pCurrentThreadMetaData + g_uiOffset[TMO_BIN_PAGE_NUM]);
			pBinMetaList = (unsigned long int*)(pCurrentThreadMetaData + g_uiOffset[TMO_BIN_META]);
			pBinUsedBtyes = (unsigned long int*)(pCurrentThreadMetaData + g_uiOffset[TMO_BIN_USED_BYTES]);
			pBinAllocReqs = (unsigned long int*)(pCurrentThreadMetaData + g_uiOffset[TMO_ALLOC_REQUESTS]);	
	
		}

		unsigned char* pBin = (unsigned char*)pBinList[uiBinIndex];
		if (NULL == pBin)
			pBin = CreateNewBin(uiPageNums, uiMetaPageNums);


		unsigned long int uiCurrentBinPageNums = pBinPageNumList[uiBinIndex];
		if (uiCurrentBinPageNums < uiPageNums)
		{
			++uiBinIndex;
			continue;
		}
		
		unsigned char* pBinMeta = (unsigned char*)pBinMetaList[uiBinIndex];
		unsigned long int uiAllocSize = 0;
		unsigned char* pAllocated =  AllocateFromBin(0, pBin, pBinMeta, g_iPageSize * uiCurrentBinPageNums, uiSize_, uiMinBlackSize_, &uiAllocSize);
		if (pAllocated)
		{
			pBinUsedBtyes[uiBinIndex] += uiAllocSize;
			pBinAllocReqs[uiBinIndex] += 1;
			
			return (void*)pAllocated; 
		}
			       

		++uiBinIndex;
		++uiActualBinIndex;
	} while (pCurrentThreadMetaData);
	
	return NULL;
}

// Create a new thread Arena
unsigned char* CreateNewThreadArena()
{
	if (NULL == CreateNewThreadMeta(NULL))
		return NULL;
	
	pthread_t self = pthread_self();
	sem_t newLock;
	sem_init(&newLock, 1, 1);
	
	///////////////////////////////////////////////////////////////////////////////////
	sem_wait(&g_semProcessLock);
	unsigned long int uiThreadCounts = g_uiRegisteredThreadCounts;
	unsigned long int uiNewThreadIndex =  uiThreadCounts % g_uiMaxThreadNums;
	unsigned long int uiProcessMetaPageIndex = uiThreadCounts / g_uiMaxThreadNums;
	

	unsigned char* pCurrentMetaPage = GetProcessMetaPage(uiProcessMetaPageIndex);
	// Need a New Page for Process Metadata
	if (NULL == pCurrentMetaPage)
	{
		pCurrentMetaPage = CreateNewProcessMetaPage(NULL);
		if (NULL == pCurrentMetaPage)
		{
			// Mmeory Allocation for the nee page failed
			sem_post(&g_semProcessLock);
			return NULL;
		}
	}
		
	*(((pthread_t*)(pCurrentMetaPage + g_uiThreadList_Offset)) + uiNewThreadIndex) = self;
	*(((unsigned long int*)(pCurrentMetaPage + g_uiThreadMetaList_Offset)) + uiNewThreadIndex) = (unsigned long int)t_pThreadMetaData;
	t_pThreadLock = ((sem_t*)(pCurrentMetaPage + g_uiThreadLockList_Offset) + uiNewThreadIndex);
	memcpy(t_pThreadLock, &newLock, sizeof(sem_t));	
	
	++uiThreadCounts;
	g_uiRegisteredThreadCounts = uiThreadCounts;
	
	sem_post(&g_semProcessLock);
	///////////////////////////////////////////////////////////////////////////////////////
	
	return t_pThreadMetaData;
}

// Free memory from another Thread Arena
// 0 : trying to free an address when that address has not been allocated yet
// ULONG_MAX : The given ptr is invalid because it is not allocated from the arenas
// Otherwise, return the size of the freed memmory
unsigned long int FreeFromAllArenas(void *ptr)
{
	if (NULL == g_pProcessMetaData)
		return ULONG_MAX;
		
	// Process Metadata are accessed with read-only operations here, so this function does not acquire a lock for Process Metadata.
	// A new Thread Arena Creation is done inside the process lock, so g_uiRegisteredThreadCounts is correctly incremented by one.
	// It is possible that when this funcion loads g_uiRegisteredThreadCounts, other information of the new Thread Arena has not been updated to memory yet ( Because of Memory ordering)
	// However, that means, memory pointed to by ptr was not allocated from the Thread Arena being created.
	// Thus, even if the new value of g_uiRegisteredThreadCounts is loaded, freeing ptr will be successfully done before this function searches on that new Thread Arena.
	// Also, this function does not search on the new Thread Arena if all the information of that new Arena has not been updated ( In case, user program provides an incorrect ptr)

	unsigned long int uiRegisteredThreadCounts = g_uiRegisteredThreadCounts;
	pthread_t self = pthread_self();
	unsigned char* pCurrentMeta = g_pProcessMetaData;
	pthread_t* pThreadList = (pthread_t*)(pCurrentMeta + g_uiThreadList_Offset);
	unsigned long* pThreadMetaList = (unsigned long int*)(pCurrentMeta + g_uiThreadMetaList_Offset);
	sem_t* pThreadLockList = (sem_t*)(pCurrentMeta + g_uiThreadLockList_Offset);
	unsigned long int uiThreadIndex = 0;
	unsigned long int uiCounts = 0;
	
	while (uiCounts < uiRegisteredThreadCounts)
	{
		if (uiThreadIndex >= g_uiMaxThreadNums)
		{
			uiThreadIndex = 0;
			pCurrentMeta = (unsigned char*)*(((unsigned long int*)pCurrentMeta) + 1);
			if (NULL == pCurrentMeta)
				return ULONG_MAX;
			
			pThreadList = (pthread_t*)(pCurrentMeta + g_uiThreadList_Offset);
			pThreadMetaList = (unsigned long int*)(pCurrentMeta + g_uiThreadMetaList_Offset);
			pThreadLockList = (sem_t*)(pCurrentMeta + g_uiThreadLockList_Offset);
		}
		
		unsigned long int uiResult = ULONG_MAX;
		
		// Old value Checking
		if (pthread_equal(0, pThreadList[uiThreadIndex]))
			continue;
		
		if (0 == pthread_equal(self, pThreadList[uiThreadIndex]))
		{
			// Old Value checking
			// Acquire a Thread Arena lock 
			if (-1 == sem_wait(pThreadLockList + uiThreadIndex))
				continue;
			
			uiResult = FreeFromThreadArena(ptr, (unsigned char*)(pThreadMetaList[uiThreadIndex]));
			sem_post(pThreadLockList + uiThreadIndex);
		}
			
		if (ULONG_MAX != uiResult)
			return uiResult; 
		
		++uiThreadIndex;
		++uiCounts;
	}
	
	return ULONG_MAX;
}



// Free memory from its own Arena
// 0 : trying to free an address when that address has not been allocated yet
// ULONG_MAX : The given ptr is invalid because it is not allocated from the arena
// Otherwise, return the size of the freed memmory
unsigned long int FreeFromThreadArena(void* ptr, unsigned char* pThreadMetaData_)
{
	if (NULL == pThreadMetaData_)
		return ULONG_MAX;
		
	unsigned char* pCurrentMeta = pThreadMetaData_;
	unsigned long int* pBinList = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN]);
	unsigned long int* pBinMetaList = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_META]);
	unsigned long int* pBinPageNumList = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_PAGE_NUM]);
	
	unsigned long int* pBinUsedBytes = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_USED_BYTES]);
	unsigned long int* pBinFreeReqs = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_FREE_REQUESTS]);

	
	unsigned long int uiBinIndex = 0;
	unsigned long int uiBinPageNums;
	
	do
	{
		if (uiBinIndex >= g_uiMaxBinNums)
		{
			uiBinIndex = 0;
			pCurrentMeta = (unsigned char*)*(((unsigned long int*)pCurrentMeta) + 1);
			if (NULL == pCurrentMeta)
				return ULONG_MAX;
			
			pBinList = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN]);
			pBinMetaList = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_META]);
			pBinPageNumList = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_PAGE_NUM]);
			pBinUsedBytes = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_USED_BYTES]);
			pBinFreeReqs = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_FREE_REQUESTS]);
		}
			
		if (0 == pBinList[uiBinIndex])
			return ULONG_MAX;

		uiBinPageNums = pBinPageNumList[uiBinIndex];

			
		unsigned long int uiTargetAddr = (unsigned long int)ptr;
		if (uiTargetAddr < pBinList[uiBinIndex]  ||
			(pBinList[uiBinIndex] + (g_iPageSize * uiBinPageNums) <= uiTargetAddr))
		{
			++uiBinIndex;
			continue;
		}
			
		unsigned char* pActualBinMetaData;

		pActualBinMetaData = (unsigned char*)(pBinMetaList[uiBinIndex]);
		unsigned long int uiResult = FreeFromBin(0, (unsigned char*)ptr, (unsigned char*)pBinList[uiBinIndex], pActualBinMetaData, g_iPageSize * uiBinPageNums, MIN_BLOCK_SIZE);
		if (ULONG_MAX != uiResult)
		{
			pBinUsedBytes[uiBinIndex] -= uiResult;
			pBinFreeReqs[uiBinIndex] += 1;
		}
		
		return uiResult;
	
	} while (pCurrentMeta);	
	
	return ULONG_MAX;
}

// Allocate memory from a Bin (Binary Search)
unsigned char* AllocateFromBin(unsigned long int uiNode_, unsigned char* pBin_, unsigned char* pMeta_, size_t uiCurrentNodeSize_, size_t uiRequestedSize_, size_t uiBlockMinSize_, unsigned long int* pAllocSize_)
{
	if (uiCurrentNodeSize_ < uiBlockMinSize_)
		return NULL;
	
	if (uiCurrentNodeSize_ < uiRequestedSize_)
		return NULL;
	
	char cState = GetNodeState(uiNode_, pMeta_);
	char cNewState = cState;
	
	if (EBBS_BOTH_FULL == cState || EBBS_ALLOCATED_AT_ONCE == cState)
		return NULL;
	
	// 0 : Free 
	// 1 : Right is used and Left is free
	// 2 : Left is used and Right is free
	// 3 : Both are used
	// 4 : Right is fully used and Left is free
	// 5 : Left is fully used and Right is free
	// 6 : Right is fully used and Left is used
	// 7 : Left is fully used and Right is used
	// 8 : Both are fully used
	// 9 : Allocated at this level ( For Free Function )
	
	size_t uiHalfNodeSize = uiCurrentNodeSize_ / 2;

	if (uiHalfNodeSize < uiRequestedSize_ || uiHalfNodeSize < uiBlockMinSize_)
	{
		if (0 != cState)
			return NULL;
		
		cNewState = EBBS_ALLOCATED_AT_ONCE;
		SetNodeState(uiNode_, pMeta_, cNewState);
			
		*pAllocSize_ = uiCurrentNodeSize_;
		return pBin_;
	}
	else
	{
		unsigned long int uiLeftNode = (uiNode_ * 2) + 1;
		unsigned char* pAllocatedAddr = AllocateFromBin(uiLeftNode, pBin_, pMeta_, uiHalfNodeSize, uiRequestedSize_, uiBlockMinSize_, pAllocSize_);
		if (pAllocatedAddr)
		{
			unsigned char ucLeftNodeState = GetNodeState(uiLeftNode, pMeta_);
			switch (cState)
			{
			// Right is free
			case EBBS_FREE:
			case EBBS_LEFT_USED_RIGHT_FREE:
			case EBBS_LEFT_FULL_RIGHT_FREE:
				if (EBBS_BOTH_FULL == ucLeftNodeState || EBBS_ALLOCATED_AT_ONCE == ucLeftNodeState) // Left is fully used
					cNewState = EBBS_LEFT_FULL_RIGHT_FREE;
				else // Part of Left is used 
					cNewState = EBBS_LEFT_USED_RIGHT_FREE;
				break;
			// Right is used
			case EBBS_RIGHT_USED_LEFT_FREE:
			case EBBS_BOTH_USED:
			case EBBS_LEFT_FULL_RIGHT_USED:
				if (EBBS_BOTH_FULL == ucLeftNodeState || EBBS_ALLOCATED_AT_ONCE == ucLeftNodeState) // Left is fully used
					cNewState = EBBS_LEFT_FULL_RIGHT_USED;
				else // Part of Left is used 
					cNewState = EBBS_BOTH_USED;
					
				break;
			// Right is fully used
			case EBBS_RIGHT_FULL_LEFT_FREE:
			case EBBS_RIGHT_FULL_LEFT_USED:
				if (EBBS_BOTH_FULL == ucLeftNodeState || EBBS_ALLOCATED_AT_ONCE == ucLeftNodeState) // Left is fully used
					cNewState = EBBS_BOTH_FULL;
				else // Part of Left is used 
					cNewState = EBBS_RIGHT_FULL_LEFT_USED;
				
				break;
			}
			
			SetNodeState(uiNode_, pMeta_, cNewState);
			return pAllocatedAddr;
		}
		else
		{
			unsigned long int uiRightNode = (uiNode_ * 2) + 2;
			pAllocatedAddr = AllocateFromBin(uiRightNode, pBin_ + uiHalfNodeSize, pMeta_, uiHalfNodeSize, uiRequestedSize_, uiBlockMinSize_, pAllocSize_);
			
			if (pAllocatedAddr)
			{
				unsigned char ucRightNodeState = GetNodeState(uiRightNode, pMeta_);
				switch (cState)
				{
				// Left is free
				case EBBS_FREE:
				case EBBS_RIGHT_USED_LEFT_FREE:
				case EBBS_RIGHT_FULL_LEFT_FREE:
					if (EBBS_BOTH_FULL == ucRightNodeState || EBBS_ALLOCATED_AT_ONCE == ucRightNodeState) // Right is fully used
						cNewState = EBBS_RIGHT_FULL_LEFT_FREE;
					else // Part of Right is used 
						cNewState = EBBS_RIGHT_USED_LEFT_FREE;
					break;
				// Left is used
				case EBBS_LEFT_USED_RIGHT_FREE:
				case EBBS_BOTH_USED:
				case EBBS_RIGHT_FULL_LEFT_USED:
					if (EBBS_BOTH_FULL == ucRightNodeState || EBBS_ALLOCATED_AT_ONCE == ucRightNodeState) // Right is fully used
						cNewState = EBBS_RIGHT_FULL_LEFT_USED;
					else // Part of Right is used 
						cNewState = EBBS_BOTH_USED;
					break;
				// Left is fully used
				case EBBS_LEFT_FULL_RIGHT_FREE:
				case EBBS_LEFT_FULL_RIGHT_USED:
					if (EBBS_BOTH_FULL == ucRightNodeState || EBBS_ALLOCATED_AT_ONCE == ucRightNodeState) // Right is fully used
						cNewState = EBBS_BOTH_FULL;
					else // Part of Right is used 
						cNewState = EBBS_LEFT_FULL_RIGHT_USED;
					break;
				}
				
				SetNodeState(uiNode_, pMeta_, cNewState);
				
			}
				
			return pAllocatedAddr;
		}
	}
	
	return NULL;
}


// Free from a Bin (Binary Search)
// 0 : trying to free an address when that address has not been allocated yet
// ULONG_MAX : The given ptr is in the current Block of the Bin (for reculsive call)
//             This does not mean the given ptr is not allocated from the Bin 
// Otherwise, return the size of the freed memmory
unsigned long int FreeFromBin(unsigned long int uiNode_, unsigned char* pAddrTobeFreed_, unsigned char* pBin_, unsigned char* pMeta_, size_t uiCurrentNodeSize_, size_t uiBlockMinSize_)
{
	if (uiCurrentNodeSize_ < uiBlockMinSize_)
		return ULONG_MAX;
	
	char cState = GetNodeState(uiNode_, pMeta_);
	char cNewState;
	if (pAddrTobeFreed_ == pBin_)
	{
		// 0 : Free 
		// 1 : Right is used and Left is free
		// 2 : Left is used and Right is free
		// 3 : Both are used
		// 4 : Right is fully used and Left is free
		// 5 : Left is fully used and Right is free
		// 6 : Right is fully used and Left is used
		// 7 : Left is fully used and Right is used
		// 8 : Both are fully used
		// 9 : Allocated at this level ( For Free Function )
		
		if (EBBS_ALLOCATED_AT_ONCE == cState)
		{
			cNewState = EBBS_FREE;
			SetNodeState(uiNode_, pMeta_, cNewState);
			return uiCurrentNodeSize_;
		}
		else
		{
			unsigned long int uiLeftNode = (uiNode_ * 2) + 1;
			size_t uiChildNodeSize = uiCurrentNodeSize_ / 2;
			unsigned long int uiResult = FreeFromBin(uiLeftNode, pAddrTobeFreed_, pBin_, pMeta_, uiChildNodeSize, uiBlockMinSize_);
			if (ULONG_MAX != uiResult)
			{
				unsigned char ucLeftNodeState = GetNodeState(uiLeftNode, pMeta_);
				switch (cState)
				{
				// Right is free
				case EBBS_LEFT_USED_RIGHT_FREE:
				case EBBS_LEFT_FULL_RIGHT_FREE:
					if (EBBS_FREE == ucLeftNodeState) // Left is free
						cNewState = EBBS_FREE;
					else// Part of Left is used 
						cNewState = EBBS_LEFT_USED_RIGHT_FREE;
					break;
				// Right is used
				case EBBS_RIGHT_USED_LEFT_FREE:
				case EBBS_BOTH_USED:
				case EBBS_LEFT_FULL_RIGHT_USED:
					if (EBBS_FREE == ucLeftNodeState) // Left is free
						cNewState = EBBS_RIGHT_USED_LEFT_FREE;
					else // Part of Left is used 
						cNewState = EBBS_BOTH_USED;
					
					break;
				// Right is fully used
				case EBBS_RIGHT_FULL_LEFT_FREE:
				case EBBS_RIGHT_FULL_LEFT_USED:
				case EBBS_BOTH_FULL:
					if (EBBS_FREE == ucLeftNodeState) // Left is free
						cNewState = EBBS_RIGHT_FULL_LEFT_FREE;
					else // Part of Left is used 
						cNewState = EBBS_RIGHT_FULL_LEFT_USED;
				
					break;
				}
			
				SetNodeState(uiNode_, pMeta_, cNewState);
			}
	
			return uiResult;
		}
	}
	else
	{
		size_t uiChildNodeSize = uiCurrentNodeSize_ / 2;
		if (pAddrTobeFreed_ < pBin_ + uiChildNodeSize)
		{
			unsigned long int uiLeftNode = (uiNode_ * 2) + 1;
			unsigned long int uiResult = FreeFromBin(uiLeftNode, pAddrTobeFreed_, pBin_, pMeta_, uiChildNodeSize, uiBlockMinSize_);
			if (ULONG_MAX != uiResult)
			{
				unsigned char ucLeftNodeState = GetNodeState(uiLeftNode, pMeta_);
				switch (cState)
				{
				// Right is free
				case EBBS_LEFT_USED_RIGHT_FREE:
				case EBBS_LEFT_FULL_RIGHT_FREE:
					if (EBBS_FREE == ucLeftNodeState) // Left is free
						cNewState = EBBS_FREE;
					else// Part of Left is used 
						cNewState = EBBS_LEFT_USED_RIGHT_FREE;
					break;
				// Right is used
				case EBBS_RIGHT_USED_LEFT_FREE:
				case EBBS_BOTH_USED:
				case EBBS_LEFT_FULL_RIGHT_USED:
					if (EBBS_FREE == ucLeftNodeState) // Left is free
						cNewState = EBBS_RIGHT_USED_LEFT_FREE;
					else // Part of Left is used 
						cNewState = EBBS_BOTH_USED;
					
					break;
				// Right is fully used
				case EBBS_RIGHT_FULL_LEFT_FREE:
				case EBBS_RIGHT_FULL_LEFT_USED:
				case EBBS_BOTH_FULL:
					if (EBBS_FREE == ucLeftNodeState) // Left is free
						cNewState = EBBS_RIGHT_FULL_LEFT_FREE;
					else // Part of Left is used 
						cNewState = EBBS_RIGHT_FULL_LEFT_USED;
				
					break;
				}
			
				SetNodeState(uiNode_, pMeta_, cNewState);
			}
	
			return uiResult;
		}
		else
		{
			unsigned long int uiRightNode = (uiNode_ * 2) + 2;
	
			unsigned long int uiResult = FreeFromBin(uiRightNode, pAddrTobeFreed_, pBin_ + uiChildNodeSize, pMeta_, uiChildNodeSize, uiBlockMinSize_);
			if (ULONG_MAX != uiResult)
			{
				unsigned char ucRightNodeState = GetNodeState(uiRightNode, pMeta_);
				switch (cState)
				{
				// Left is free
				case EBBS_RIGHT_USED_LEFT_FREE:
				case EBBS_RIGHT_FULL_LEFT_FREE:
					if (EBBS_FREE == ucRightNodeState) // Right is free
						cNewState = EBBS_FREE;
					else // Part of Right is used 
						cNewState = EBBS_RIGHT_USED_LEFT_FREE;
					break;
				// Left is used
				case EBBS_LEFT_USED_RIGHT_FREE:
				case EBBS_BOTH_USED:
				case EBBS_RIGHT_FULL_LEFT_USED:
					if (EBBS_FREE == ucRightNodeState) // Right is free
						cNewState = EBBS_LEFT_USED_RIGHT_FREE;
					else // Part of Right is used 
						cNewState = EBBS_BOTH_USED;
					break;
				// Left is fully used
				case EBBS_LEFT_FULL_RIGHT_FREE:
				case EBBS_LEFT_FULL_RIGHT_USED:
				case EBBS_BOTH_FULL:
					if (EBBS_FREE == ucRightNodeState) // Right is free
						cNewState = EBBS_LEFT_FULL_RIGHT_FREE;
					else // Part of Right is used 
						cNewState = EBBS_LEFT_FULL_RIGHT_USED;
					break;
				}
				
				SetNodeState(uiNode_, pMeta_, cNewState);
			}

			return uiResult;
		}
		
	}
	
	return ULONG_MAX;
}

// Set a new state value to a Node (Block)
void SetNodeState(unsigned long int uiNodeIndex_, unsigned char* pMeta_, unsigned char ucState_)
{
	int iIndex = uiNodeIndex_ / BLOCKS_IN_ONE_BYTE;
	int iOffset = (uiNodeIndex_ % BLOCKS_IN_ONE_BYTE) * BITS_PER_BLOCK_METADATA;
	int iReverseOffset = BITS_PER_BLOCK_METADATA  - iOffset;
	
	unsigned char cNewState = ucState_ << iReverseOffset;
	unsigned char cBuddyMask = 0xf0 >> iReverseOffset;
	unsigned char cBuddyState = (*(pMeta_ + iIndex) & cBuddyMask);
	*(pMeta_ + iIndex) = cBuddyState | cNewState;
}

// Get the state value of a Node (Block)
unsigned char GetNodeState(unsigned long int  uiNodeIndex_, unsigned char* pMeta_)
{
	int iIndex = uiNodeIndex_ / BLOCKS_IN_ONE_BYTE;
	int iOffset = (uiNodeIndex_ % BLOCKS_IN_ONE_BYTE) * BITS_PER_BLOCK_METADATA;
	int iReverseOffset = BITS_PER_BLOCK_METADATA  - iOffset;
	unsigned char mask = 0xf0 >> iOffset;
	unsigned char cState = (*(pMeta_ + iIndex) & mask) >> iReverseOffset;
	
	return cState;
}


// Get the ith page of the Process Metadata
unsigned char* GetProcessMetaPage(unsigned long int uiPageIndex)
{
	unsigned char* pCurrentMetaPage = g_pProcessMetaData;
	
	unsigned long int i = 0;
	while (pCurrentMetaPage)
	{
		if (i >= uiPageIndex)
			return pCurrentMetaPage;
		
		++i;
		pCurrentMetaPage = (unsigned char*)*(((unsigned long int*)pCurrentMetaPage) + 1);
	}
	
	return pCurrentMetaPage;
}

// Get the ith page of the Thread Arena Metadata
unsigned char* GetThreadMetaPage(unsigned long int uiPageIndex)
{
	unsigned char* pCurrentMetaPage = t_pThreadMetaData;
	
	unsigned long int i = 0;
	while (pCurrentMetaPage)
	{
		if (i >= uiPageIndex)
			return pCurrentMetaPage;
		
		++i;
		pCurrentMetaPage = (unsigned char*)*(((unsigned long int*)pCurrentMetaPage) + 1);
	}
	
	return pCurrentMetaPage;
}

// Get the last page of the Process Metadata
unsigned char* GetLastProcessMetaPage()
{
	unsigned char* pCurrentMetaPage = g_pProcessMetaData;
	unsigned char* pPrevMetaPage = g_pProcessMetaData;

	while (pCurrentMetaPage)
	{
		pPrevMetaPage = pCurrentMetaPage;
		pCurrentMetaPage = (unsigned char*)*(((unsigned long int*)pCurrentMetaPage) + 1);
	}
	
	return pPrevMetaPage;
}

// Get the last page of the Thread Arena Metadata
unsigned char* GetLastThreadMetaPage()
{
	unsigned char* pCurrentMetaPage = t_pThreadMetaData;
	unsigned char* pPrevMetaPage = t_pThreadMetaData;

	while (pCurrentMetaPage)
	{
		pPrevMetaPage = pCurrentMetaPage;
		pCurrentMetaPage = (unsigned char*)*(((unsigned long int*)pCurrentMetaPage) + 1);
	}
	
	return pPrevMetaPage;
}

// To avoid allocating a new page for every new Bin
// Find available space among Metadata page pool, which are already in use, but has some space.
unsigned char* GetLargeBinMetaPage(unsigned long int uiMetaSize_)
{
	if (NULL == t_pThreadMetaData)
		return NULL;
	
	unsigned char* pCurrentMeta = t_pThreadMetaData;
	unsigned long int* pBinMetaPageList = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_META_POOL]);
	if (0 == (*pBinMetaPageList))
		return NULL;
	
	unsigned long int* pBinMetaPageNumsList = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_META_PAGE_NUM]);
	unsigned long int* pBinMetaPageOffsetLists = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_META_OFFSET]);
	
	unsigned long int uiMetaIndex = 0;
	unsigned long int uiTotalMetaIndex = 0;
	do
	{
		if (uiMetaIndex >= g_uiMaxBinNums)
		{
			uiMetaIndex = 0;
			pCurrentMeta = (unsigned char*)*(((unsigned long int*)pCurrentMeta) + 1);
			if (0 == pCurrentMeta)
				return NULL;
			
			pBinMetaPageList = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_META_POOL]);		
			pBinMetaPageNumsList = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_META_PAGE_NUM]);
			pBinMetaPageOffsetLists = (unsigned long int*)(pCurrentMeta + g_uiOffset[TMO_BIN_META_OFFSET]);
		}
		
		if (0 == pBinMetaPageList[uiMetaIndex])
			return NULL;
		
		unsigned long int uiPreviousOffset = pBinMetaPageOffsetLists[uiMetaIndex];
		unsigned long int uiRemainSize = (g_iPageSize * pBinMetaPageNumsList[uiMetaIndex]) - uiPreviousOffset;
		if (uiRemainSize >= uiMetaSize_)
		{
			pBinMetaPageOffsetLists[uiMetaIndex] = pBinMetaPageOffsetLists[uiMetaIndex] + uiMetaSize_;
			return (((unsigned char*)pBinMetaPageList[uiMetaIndex]) + uiPreviousOffset);
		}

		++uiMetaIndex;
		++uiTotalMetaIndex;
	} while (pCurrentMeta);

	return NULL;
}



