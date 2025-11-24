/*
 * dynamicMemory.c
 *
 *  Created on: 22 dic 2018
 *      Author: ugo
 */

#include <stdio.h>
#include <stdlib.h>
#define _REENTRANT
#include <pthread.h>
#include <string.h>
#include "dynamicMemory.h"
#include "errorHandling.h"
#include "../debugHelper.h"

// --- GLOBALS ---

// Total memory allocated (Global Counter)
static unsigned long long int theTotalMemory = 0;

// Mutex for the global counter
static pthread_mutex_t memoryCountMutex = PTHREAD_MUTEX_INITIALIZER;

// Mutex for the linked list operations
// (Added to prevent race conditions when modifying next/prev pointers)
static pthread_mutex_t memoryListMutex = PTHREAD_MUTEX_INITIALIZER;

// --- HELPER FUNCTIONS ---

void DYNMEM_Init(void)
{
    // With PTHREAD_MUTEX_INITIALIZER used above, explicit runtime init 
    // is often unnecessary on modern POSIX systems. 
    // However, to match the original API and ensure total reset:
    
    static int initialized = 0;
    static pthread_mutex_t initLock = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&initLock);
    if (initialized == 0)
    {
        theTotalMemory = 0;
        // In case standard initializers weren't used or reset is needed
        // pthread_mutex_init(&memoryCountMutex, NULL); 
        // pthread_mutex_init(&memoryListMutex, NULL);
        initialized = 1;
    }
    pthread_mutex_unlock(&initLock);
}

unsigned long long int DYNMEM_GetTotalMemory(void)
{
    pthread_mutex_lock(&memoryCountMutex);
    unsigned long long int mem = theTotalMemory;
    pthread_mutex_unlock(&memoryCountMutex);
    return mem;
}

unsigned long long int DYNMEM_IncreaseMemoryCounter(unsigned long long int theSize)
{
    pthread_mutex_lock(&memoryCountMutex);
    theTotalMemory += theSize;
    unsigned long long int ret = theTotalMemory;
    pthread_mutex_unlock(&memoryCountMutex);
    return ret;
}

unsigned long long int DYNMEM_DecreaseMemoryCounter(unsigned long long int theSize)
{
    pthread_mutex_lock(&memoryCountMutex);
    // Prevent underflow check could be added here
    theTotalMemory -= theSize;
    unsigned long long int ret = theTotalMemory;
    pthread_mutex_unlock(&memoryCountMutex);
    return ret;
}

// --- CORE FUNCTIONS ---

void *DYNMEM_malloc(size_t bytes, DYNMEM_MemoryTable_DataType **memoryListHead, char * theName)
{
    void *memory = NULL;
    DYNMEM_MemoryTable_DataType *newItem = NULL;

    // 1. Allocation (done outside lock for concurrency performance)
    memory = calloc(bytes, 1);
    if (!memory)
    {
        report_error_q("Memory allocation error, out of memory.", __FILE__, __LINE__, 0);
        return NULL;
    }

    newItem = calloc(1, sizeof(DYNMEM_MemoryTable_DataType));
    if (!newItem)
    {
        free(memory); // Rollback
        report_error_q("Memory allocation error, no room for memory list item.", __FILE__, __LINE__, 0);
        return NULL;
    }

    // 2. Prepare Metadata
    newItem->address = memory;
    newItem->size = bytes;
    strncpy(newItem->theName, theName, sizeof(newItem->theName) - 1);
    newItem->theName[sizeof(newItem->theName) - 1] = '\0';

    // 3. Critical Section: Update List
    pthread_mutex_lock(&memoryListMutex);

    newItem->nextElement = *memoryListHead;
    newItem->previousElement = NULL;

    if ((*memoryListHead) != NULL)
    {
        (*memoryListHead)->previousElement = newItem;
    }
    *memoryListHead = newItem;

    pthread_mutex_unlock(&memoryListMutex);

    // 4. Update Global Counter
    DYNMEM_IncreaseMemoryCounter(bytes + sizeof(DYNMEM_MemoryTable_DataType));

    return memory;
}

void *DYNMEM_realloc(void *theMemoryAddress, size_t bytes, DYNMEM_MemoryTable_DataType **memoryListHead)
{
    if (theMemoryAddress == NULL)
    {
        return DYNMEM_malloc(bytes, memoryListHead, "Realloc");
    }

    DYNMEM_MemoryTable_DataType *temp = NULL;
    DYNMEM_MemoryTable_DataType *found = NULL;

    // 1. Critical Section: Find the metadata FIRST
    pthread_mutex_lock(&memoryListMutex);
    
    for (temp = (*memoryListHead); temp != NULL; temp = temp->nextElement)
    {
        if (temp->address == theMemoryAddress)
        {
            found = temp;
            break;
        }
    }

    if (!found)
    {
        pthread_mutex_unlock(&memoryListMutex);
        report_error_q("Unable to reallocate memory not previously allocated", __FILE__, __LINE__, 0);
        return NULL;
    }

    // 2. Perform System Realloc (While holding list lock to prevent free race, 
    //    or unlock here if you accept the risk to gain performance. 
    //    Holding it is safer.)
    
    void *newMemory = realloc(theMemoryAddress, bytes);

    if (newMemory)
    {
        // Calculation for global counter update
        size_t oldSize = found->size;
        
        // Update node
        found->address = newMemory;
        found->size = bytes;
        
        pthread_mutex_unlock(&memoryListMutex);

        // Update Globals
        if (bytes > oldSize)
            DYNMEM_IncreaseMemoryCounter(bytes - oldSize);
        else
            DYNMEM_DecreaseMemoryCounter(oldSize - bytes);

        return newMemory;
    }
    else
    {
        // Realloc failed. Old memory block is still valid.
        pthread_mutex_unlock(&memoryListMutex);
        report_error_q("Memory reallocation error, out of memory.", __FILE__, __LINE__, 0);
        // Do NOT return NULL if you want to act like standard realloc? 
        // Actually standard realloc returns NULL on failure, leaving old block. 
        return NULL; 
    }
}

void DYNMEM_free(void *f_address, DYNMEM_MemoryTable_DataType **memoryListHead)
{
    if (f_address == NULL) return;

    DYNMEM_MemoryTable_DataType *temp = NULL;
    DYNMEM_MemoryTable_DataType *found = NULL;

    // 1. Critical Section: Search and Unlink
    pthread_mutex_lock(&memoryListMutex);

    for (temp = (*memoryListHead); temp != NULL; temp = temp->nextElement)
    {
        if (temp->address == f_address)
        {
            found = temp;
            break;
        }
    }

    if (!found)
    {
        pthread_mutex_unlock(&memoryListMutex);
        report_error_q("Unable to free memory not previously allocated", __FILE__, __LINE__, 1);
        return;
    }

    // Unlink from list
    if (found->previousElement)
        found->previousElement->nextElement = found->nextElement;

    if (found->nextElement)
        found->nextElement->previousElement = found->previousElement;

    if (found == (*memoryListHead))
        (*memoryListHead) = found->nextElement;

    pthread_mutex_unlock(&memoryListMutex);

    // 2. Update Counter and Free
    DYNMEM_DecreaseMemoryCounter(found->size + sizeof(DYNMEM_MemoryTable_DataType));
    
    free(found->address); // Free actual memory
    free(found);          // Free metadata
}

void DYNMEM_freeAll(DYNMEM_MemoryTable_DataType **memoryListHead)
{
    DYNMEM_MemoryTable_DataType *current = NULL;
    DYNMEM_MemoryTable_DataType *next = NULL;
    unsigned long long int totalSizeRemoved = 0;

    // 1. Critical Section: Wipe entire list
    pthread_mutex_lock(&memoryListMutex);

    current = *memoryListHead;
    while (current != NULL)
    {
        next = current->nextElement;

        totalSizeRemoved += (current->size + sizeof(DYNMEM_MemoryTable_DataType));
        
        free(current->address);
        free(current);

        current = next;
    }
    
    *memoryListHead = NULL;

    pthread_mutex_unlock(&memoryListMutex);

    // 2. Update Global Counter Once
    if (totalSizeRemoved > 0)
    {
        DYNMEM_DecreaseMemoryCounter(totalSizeRemoved);
    }
}

void DYNMEM_dump(DYNMEM_MemoryTable_DataType *memoryListHead)
{
    int count = 0;
    unsigned long long int total = 0;

    // Dump is read-only, but we need to lock to prevent the list 
    // changing while we print (segfault prevention).
    pthread_mutex_lock(&memoryListMutex);

    my_printf("\n==== DYNMEM Memory Dump ====\n");

    for (DYNMEM_MemoryTable_DataType *current = memoryListHead; current != NULL; current = current->nextElement)
    {
        my_printf("Block %d:\n", count + 1);
        my_printf("  Address   : %p\n", current->address);
        my_printf("  Size      : %zu bytes\n", current->size);
        my_printf("  Label     : %s\n", current->theName);
        my_printf("  Block MetaSize: %zu bytes\n", sizeof(DYNMEM_MemoryTable_DataType));
        total += current->size + sizeof(DYNMEM_MemoryTable_DataType);
        count++;
    }

    pthread_mutex_unlock(&memoryListMutex);

    my_printf("\nTotal blocks      : %d\n", count);
    my_printf("Total memory used : %llu bytes (including metadata)\n", total);
    my_printf("Global counter    : %llu bytes\n", DYNMEM_GetTotalMemory());
    my_printf("=============================\n");
}