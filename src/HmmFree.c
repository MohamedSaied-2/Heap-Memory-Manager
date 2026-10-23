/*
    File : HmmFree.c
    Desc : Heap Memory Manager - Deallocation, Immediate Coalescing, and OS Shrinking
*/

#include <unistd.h>
#include <stdint.h>
#include "Hmm.h"
#include "HmmInternal.h"

/* ------------------------------------------------------------------ */
/* Internal Helper Functions                                          */
/* ------------------------------------------------------------------ */

// This function acts as a safety validator for allocated payload pointers before freeing.
uint8_t HmmIsLivePayload(const void *ptr)
{
  if (ptr == NULL || g_heap_start == NULL) {
    return 0;
  }

  if ((uintptr_t)ptr < (uintptr_t)g_heap_start || (uintptr_t)ptr >= (uintptr_t)g_heap_end) {
    return 0;
  }

  if ((uintptr_t)ptr % ALIGNMENT != 0) {
    return 0;
  }

  if ((uint8_t*) ptr < (uint8_t*) g_heap_start + HMM_HEADER_SIZE || (uint8_t*) ptr >= (uint8_t*) g_heap_end - sizeof(block_footer_t)) {
    return 0;
  }

  block_header_t* block = HMM_BLOCK_OF(ptr);

  if (block->is_free) {
    return 0;
  }

  if (block->size < MIN_BLOCK_SIZE || block->size > (size_t)(g_heap_end - (uint8_t*)block)) {
    return 0;
  }

  block_footer_t* footer = (block_footer_t*)((uint8_t*)block + block->size - sizeof(block_footer_t));

  if (footer->size != block->size || footer->is_free != block->is_free) {
    return 0;
  }

  return 1;
}

// This function acts as the immediate coalescing, insertion, and OS shrinking engine.
void HmmCoalesceAndInsert(block_header_t *block)
{
  if (block == NULL || block->is_free == 0) {
    return;
  }

  if ((uint8_t*)block < g_heap_start || (uint8_t*)block >= g_heap_end) {
    return;
  }

  block_header_t* right_neighbor = (block_header_t*)((uint8_t*)block + block->size);

  size_t new_size = block->size;

  if (right_neighbor < (block_header_t*)g_heap_end && right_neighbor->is_free) {
    HmmFreeListRemove(right_neighbor);
    new_size += right_neighbor->size;
  }

  if ((uint8_t*)block != g_heap_start) {
    block_footer_t* left_neighborF = (block_footer_t*)((uint8_t*)block - sizeof(block_footer_t));
    block_header_t* left_neighbor = (block_header_t*)((uint8_t*)left_neighborF - left_neighborF->size + sizeof(block_footer_t));

    if ((uint8_t*)left_neighbor >= g_heap_start && left_neighbor->is_free) {
      HmmFreeListRemove(left_neighbor);
      new_size += left_neighbor->size;
      block = left_neighbor;
    }
  }

  HmmSetTags(block, new_size, 1);

  if ((uint8_t*)block + new_size == g_heap_end && sbrk(0) == (void*)g_heap_end) {
    g_heap_end -= new_size;
    return;
  }

  HmmFreeListInsert(block);
}


/* ------------------------------------------------------------------ */
/* Public API Implementation                                          */
/* ------------------------------------------------------------------ */

// This function acts as the public entry point for deallocating memory.
HMM_NO_ASAN void HmmFree(void *ptr)
{ 
  if (ptr == NULL || !HmmIsLivePayload(ptr)) {
    return;
  }

  block_header_t* block = HMM_BLOCK_OF(ptr);

  HmmSetTags(block, block->size, 1);
  HmmCoalesceAndInsert(block);
  return;
}