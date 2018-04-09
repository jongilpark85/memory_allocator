#include <stddef.h>

#define DEFAULT_PAGE_SIZE 4096		// Default Page Size
#define BITS_PER_BLOCK_METADATA 4	// The number of bits used to describe the state of a block in a Bin
#define BLOCKS_IN_ONE_BYTE 2		// The number of blocks that one byte can describe
#define MIN_MEMORY_ALIGNMENT 8		// The minimun boundary of memory allocation. (ex) malloc(1) still allocates 8 bytes internally)
#define MIN_BLOCK_SIZE 8			// The size of the smallest block (in Byte) 

// When no bins are available, malloc() will internally allocate new memory for a new bin.
// A bin consists of a certain number of pages.
// If this number is too small, mmap will be called more often, which lowers the performance.
#define MIN_NEW_PAGE_NUMS 128		// The minimun number of pages a Bin occupies

// Metadata are managed in three levels: Process, Thread, and Bin
// Process Metadata contain information of threads. (Per-thread entry data)to access Metadata of each Thread Arena . ( Each thread has its own Arena)
// Thread Arena Metadata contain information to access Metadata of each Bin. ( Each thread arena can have multiple bins)
// Bin Metadata contain information of availabity of each block of a Bin. ( Each bin needs its own metadata)


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process MetaData 
// The Current address of itself. (for validity checking) 
// The Address of the next Metadata. ( like a list for unlimited expansion)
// Below sections are arrays because there could be multiple threads.
// The ID of each thread.
// The address of first page of each Thread Arena Metadata.
// The lock(Semaphore) each thread uses. (For when a thread frees memory allocated from another thread)

// In most cases, one page is enough to store Process Metadata.
// For example, let's assume that this library runs on a 64 bit machine.
// The current address requires 8 bytes 
// The next address requires 8 bytes
// sizeof(pthread_t) requires 8 bytes
// The address of first page of each Thread Arena Metadata requires 8 bytes..
// sizeof(sem_t) requires 32 bytes long

// Except the first 16 bytes, there remain 4080 bytes assuming the page size is 4096 bytes.
// 48 (8 + 32 +8) bytes are required per thread, so one page can store information of 85 (4080 / 48) Thread Arenas. 
// If user program creats 100 threads, another page is allocated to store information of the rest 25 threads.
// Then, two pages are used to store Process MetaData and managed as a list.
// That's why Process MetaData stores the address of next Metadata page.
// 85 is just an example. The number varies depending on a system.

// In short, Process Metadata are stored and managed in a list of arrays.
// Process Metadata itself is managed in a list. ( Unlimited expansion not requiring large coninuous memory space)
// Contents of Process Metadata are managed in an array. ( Fast traversing/ Better performance )


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Thread Arena MetaData
// the current address of itself. (for validity checking) 
// the address of the next Metadata. ( like a list for unlimited expansion)
// The size of the Thread Arena ( Sum of the size of each Bin)
// the address of Bins. (Each Thread Arena can have multiple Bins)
// the number of pages each Bin uses. (which represents the size of each bin) 
// the address of Metadata for each Bin, 
// the address of each MetaData Page ( Several Bins can share the same page to store their Metadata)
// the offset in each MetaData Page( to indicate where new Metadata can be stored)

// Metadata of each Thread Arena are also managed in a list of arrays.
// Thread Arena Metadata itself is managed in a list. ( Unlimited expansion not requiring large coninuous memory space)
// Contents of Thread Arena Metadata are managed in an array. ( Fast traversing/ Better performance )

// Offsets to each array in a Thread Arena Metadata page
// 0: The start address of each Bin 
// 1: The number of pages each Bin uses
// 2: The start address of the page wherhe the Metadata of each Bin is stored
// 4: The number of pages used to store the MetaData of each Bin 
//    This is because if a Bin uses a large number of pages, mulple pages are used to store the Metadata for that Bin.
// 5: Offset from TMO_BIN_META ( Several Bins can share the same page to store their MetaData.
//    For example, if the first Bin uses just a page, then the size of the Metadata of that bin is 512 bytes.
//    If the second Bin also uses a page, then the Metadata of the second Bin can be stored in the same page as the first Bin's MetaData.

// For malloc_stats() function ( TMO_BIN_USED_BYTES, TMO_ALLOC_REQUESTS, TMO_FREE_REQUESTS)
// 6: The number of bytes currently allocated to the user program from each Bin. 
// 7: The number of memory allocation reqeusts on each Bin
// 8: The number of memory release requests on each Bin
enum THREAD_METADATA_OFFSET
{
	TMO_BIN               = 0,	
	TMO_BIN_PAGE_NUM,			
	TMO_BIN_META,				
	TMO_BIN_META_POOL,			
	TMO_BIN_META_PAGE_NUM,						
	TMO_BIN_META_OFFSET,		
	TMO_BIN_USED_BYTES,			
	TMO_ALLOC_REQUESTS,			
	TMO_FREE_REQUESTS,			
	TMO_MAX,
};


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Bin Metadata 
// Bin Metadata for a Bin is managed in an array that contain each node's state
// The array is actually a binary tree

// This malloc library uses Buddy Allocation with a binary tree in the form of an array
// State of each block( 4 bits can represent 16 different states)
enum BUDDY_BLOCK_STATE
{
	EBBS_FREE                 = 0, // 0 : Free 
	EBBS_RIGHT_USED_LEFT_FREE,	// 1 : Right is used and Left is free
	EBBS_LEFT_USED_RIGHT_FREE,	// 2 : Left is used and Right is free
	EBBS_BOTH_USED,				// 3 : Both are used
	EBBS_RIGHT_FULL_LEFT_FREE,	// 4 : Right is fully used and Left is free
	EBBS_LEFT_FULL_RIGHT_FREE,	// 5 : Left is fully used and Right is free
	EBBS_RIGHT_FULL_LEFT_USED,	// 6 : Right is fully used and Left is used
	EBBS_LEFT_FULL_RIGHT_USED,	// 7 : Left is fully used and Right is used
	EBBS_BOTH_FULL,				// 8 : Both are fully used
	EBBS_ALLOCATED_AT_ONCE,		// 9 : Allocated at this level ( For Free Function )
	EBBS_MAX,					// 10
};


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Functions
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Memory allocation is only managed with its own Arena
// Allocate memory from its own Arena
void* MallocFromThreadArena(size_t size, unsigned long int uiMinBlackSize_);

// Allocate memory from a Bin (Binary Search)
unsigned char* AllocateFromBin(unsigned long int uiNode_, unsigned char* pBin_, unsigned char* pMeta_, size_t uiCurrentNodeSize_, size_t uiRequestedSize_, size_t uiBlockMinSize_, unsigned long int* pAllocSize_);

// Free memory from other Threads' Arenas
unsigned long int FreeFromAllArenas(void *ptr);

// Free memory from its own Arena
unsigned long int FreeFromThreadArena(void* ptr, unsigned char* pThreadMetaData_);

// Free from a Bin (Binary Search)
unsigned long int FreeFromBin(unsigned long int uiNode_, unsigned char* pAddrTobeFreed_, unsigned char* pBin_, unsigned char* pMeta_, size_t uiCurrentNodeSize_, size_t uiBlockMinSize_);

// Print Malloc Statistics of each Arena
void MallocStatsThreadArena(unsigned char* pThreadMetaData_);

// Allocates uiSize_ bytes. The returned memory address will be a multiple of uiAlignment_, which must be a power of two.
void* AllocateMemory(size_t uiAlignment_, size_t uiSize_);

// Free the memory space pointed to by ptr.
void FreeMemory(void* ptr);

// Print malloc statistics
void MallocStats();

// Get the state value of a Node (Block)
unsigned char GetNodeState(unsigned long int uiNodeIndex_, unsigned char* pMeta_);

// Set a new state value to a Node (Block)
void SetNodeState(unsigned long int uiNodeIndex_, unsigned char* pMeta_, unsigned char ucState_);

// To avoid allocating a new page for every new Bin
// Find available space among Metadata page pool, which are already in use, but has some space.
unsigned char* GetLargeBinMetaPage(unsigned long int uiMetaSize_);

// Get the ith page of the Process Metadata
unsigned char* GetProcessMetaPage(unsigned long int uiPageIndex);

// Get the last page of the Process Metadata
unsigned char* GetLastProcessMetaPage();

// Create a new metadata page for the Thread Arena
unsigned char* CreateNewProcessMetaPage(unsigned char* pNew_);

// Create a new thread Arena
unsigned char* CreateNewThreadArena();

// Get the ith page of the Thread Arena Metadata
unsigned char* GetThreadMetaPage(unsigned long int uiPageIndex);

// Get the last page of the Thread Arena Metadata
unsigned char* GetLastThreadMetaPage();

// Create a new metadata page for the Thread Arena
unsigned char* CreateNewThreadMeta(unsigned char* pNew_);

// Create a new Bin
unsigned char* CreateNewBin(unsigned long int uiPageNums_, unsigned long int uiMetaPagesNums_);


