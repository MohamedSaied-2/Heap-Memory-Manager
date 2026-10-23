/*
    File : HmmAlloc.c
    Desc : Heap Memory Manager - Core Allocation Engine, Init, Calloc, Realloc, and Size
*/

#include <string.h>
#include <unistd.h>
#include "Hmm.h"
#include "HmmInternal.h"

/* ------------------------------------------------------------------ */
/* Global Module State                                                */
/* ------------------------------------------------------------------ */

// Head of the explicit doubly-linked free list
block_header_t *g_free_list_head = NULL;

// Heap boundary tracking pointers for real sbrk management
uint8_t *g_heap_start = NULL;
uint8_t *g_heap_end   = NULL;
uint8_t g_heap_growable = 1U; // 1 = grow via sbrk, 0 = fixed pool (via HmmInit)


/* ------------------------------------------------------------------ */
/* Internal Helper Functions                                          */
/* ------------------------------------------------------------------ */

// This function acts as a boundary tag synchronizer.
void HmmSetTags(block_header_t *block, size_t size, uint8_t is_free)
{
  block -> size = size;
  block -> is_free = is_free;
  block_footer_t* footer = (block_footer_t*)((uint8_t*)block + size - sizeof(block_footer_t));
  footer -> size = size;
  footer -> is_free = is_free;
  memset(block -> padding, 0, sizeof(block -> padding));
  memset(footer -> padding, 0, sizeof(footer -> padding));
}

// This function acts as an O(1) unlinker for the explicit free list.
void HmmFreeListRemove(block_header_t *block)
{
  if (block -> as.free_links.prev != NULL) {
    block -> as.free_links.prev -> as.free_links.next = block -> as.free_links.next;
  } else {
    g_free_list_head = block -> as.free_links.next;
  }

  if (block -> as.free_links.next != NULL) {
    block -> as.free_links.next -> as.free_links.prev = block -> as.free_links.prev;
  } 

  block -> as.free_links.prev = NULL;
  block -> as.free_links.next = NULL;
}

//This function acts as an O(1) inserter at the head of the free list.
void HmmFreeListInsert(block_header_t *block)
{
  block -> as.free_links.prev = NULL;
  block -> as.free_links.next = g_free_list_head;
  if (g_free_list_head != NULL) {
    g_free_list_head -> as.free_links.prev = block;
  }
  g_free_list_head = block;
}

// This function acts as a real sbrk heap extender.
block_header_t *GrowHeap(size_t total_bytes)
{
  #define INITIAL_HEAP_POOL_SIZE (1024 * 1024 * 4) // e.g., 4 MB pre-reserved pool

  if (g_heap_start == NULL) {
    void* initial_break = sbrk(INITIAL_HEAP_POOL_SIZE);
    if (initial_break == (void*)-1) {
      return NULL;
    }
    g_heap_start = (uint8_t*)initial_break;
    g_heap_end = g_heap_start;
  }

  block_header_t* last_block = NULL;

  if (g_heap_end > g_heap_start) {
    block_footer_t* last_footer = (block_footer_t*)(g_heap_end - sizeof(block_footer_t));

    if (last_footer->is_free) {
      last_block = (block_header_t*)(g_heap_end - last_footer->size);

      if (total_bytes <= last_footer->size) {
        return last_block;
      }
    }
  }

  // Check if simulated heap pool has enough space
  size_t needed = last_block ? (total_bytes - last_block->size) : total_bytes;
  if ((size_t)(g_heap_end - g_heap_start) + needed > INITIAL_HEAP_POOL_SIZE) {
    return NULL; // Out of simulated heap space
  }

  // Advance the SIMULATED program break (NO sbrk call here!)
  g_heap_end += needed;

  if (last_block != NULL) {
    HmmFreeListRemove(last_block);
    HmmSetTags(last_block, total_bytes, 1);
    HmmFreeListInsert(last_block);
    return last_block;
  }

  block_header_t* new_block = (block_header_t*)(g_heap_end - total_bytes);
  HmmSetTags(new_block, total_bytes, 1);
  HmmFreeListInsert(new_block);

  return new_block;
}

/* ------------------------------------------------------------------ */
/* Public API Implementation                                          */
/* ------------------------------------------------------------------ */

// This function acts as the custom malloc allocation engine.
HMM_NO_ASAN void *HmmAlloc(size_t size)
{
  if (size == 0 || size > SIZE_MAX / 2) {
    return NULL;
  }

  size_t total_size = HMM_TOTAL_SIZE(size);

  for (block_header_t* current = g_free_list_head; current != NULL; current = current->as.free_links.next) {
    if (current -> size >= total_size) {
      size_t left = current -> size - total_size;

      if (left >= SPLIT_THRESHOLD) {
        block_header_t* new = (block_header_t*) ((uint8_t*)current + total_size);
        HmmFreeListRemove(current);
        HmmFreeListInsert(new);
        HmmSetTags(new , left , 1);
        
        HmmSetTags(current , total_size , 0);
        return HMM_PAYLOAD(current);
      }
      else {
        HmmFreeListRemove(current);
        HmmSetTags(current , current -> size , 0);
        return HMM_PAYLOAD(current);
      }
    } 
  }

  if (g_heap_growable == 1U) {
    block_header_t* new_block = GrowHeap(total_size);
    if (new_block == NULL) {
      return NULL;
    }

    HmmFreeListRemove(new_block);
    HmmSetTags(new_block , total_size , 0);
    return HMM_PAYLOAD(new_block);
  }

  return NULL;
}

//This function acts as the custom calloc zero-initialization wrapper.
HMM_NO_ASAN void *hmmCalloc(size_t nmemb, size_t size)
{
  if (size != 0 && nmemb > SIZE_MAX / size) {
    return NULL;
  }

  size_t total_size = nmemb * size;

  void *ptr = HmmAlloc(total_size);

  if (ptr != NULL) {
    memset(ptr, 0, total_size);
    return ptr;
  }

  return NULL;
}

// This function acts as the custom realloc block resizer.
HMM_NO_ASAN void *hmmReAlloc(void *ptr, size_t reqSize)
{
  if (ptr == NULL) {
    return HmmAlloc(reqSize);
  }
  
  if (reqSize == 0) {
    HmmFree(ptr);
    return NULL;
  }

  if (reqSize > SIZE_MAX / 2 || !HmmIsLivePayload(ptr)) {
    return NULL;
  }

  block_header_t* current_block = HMM_BLOCK_OF(ptr);
  size_t total = HMM_TOTAL_SIZE(reqSize);

  if (total <= current_block->size) {
    size_t left = current_block->size - total;

    if (left >= SPLIT_THRESHOLD) {
      HmmSetTags(current_block, total, 0);
      block_header_t* new_block = (block_header_t*)((uint8_t*)current_block + total);
      HmmSetTags(new_block, left, 1);
      HmmCoalesceAndInsert(new_block);
    }

    return ptr;
  }

  void* new_ptr = HmmAlloc(reqSize);

  if (new_ptr == NULL) {
    return NULL;
  }

  memcpy(new_ptr, ptr, current_block->size - HMM_HEADER_SIZE - sizeof(block_footer_t));
  HmmFree(ptr);
  return new_ptr;
}

// This function acts as a payload size query utility.
HMM_NO_ASAN size_t hmmSize(void *ptr)
{
  if (ptr == NULL || !HmmIsLivePayload(ptr)) {
    return 0;
  }

  block_header_t* block = HMM_BLOCK_OF(ptr);
  return block->size - HMM_HEADER_SIZE - sizeof(block_footer_t);
}