/*
    File : HmmInternal.h
    Desc : Cleaned internal state and helpers shared between HMM source files
*/

#ifndef HMM_INTERNAL_H
#define HMM_INTERNAL_H

#include "Hmm.h"
#include <unistd.h>
#include <stdint.h>

/* Keep ASan out of the custom allocator to prevent false flags */
#if defined(__GNUC__) || defined(__clang__)
#define HMM_NO_ASAN __attribute__((no_sanitize_address))
#else
#define HMM_NO_ASAN
#endif

/* Per-block metadata cost: header + footer */
#define HMM_OVERHEAD (HMM_HEADER_SIZE + sizeof(block_footer_t))

/* Shared global module states */
extern block_header_t *g_free_list_head;
extern uint8_t *g_heap_start;
extern uint8_t *g_heap_end;
extern uint8_t g_heap_growable; /* 1 = grow via real sbrk, 0 = fixed pool */

/* Essential boundary tag and free list helper prototypes */
HMM_NO_ASAN void HmmSetTags(block_header_t *block, size_t size, uint8_t is_free);
HMM_NO_ASAN void HmmFreeListRemove(block_header_t *block);
HMM_NO_ASAN void HmmFreeListInsert(block_header_t *block);
HMM_NO_ASAN uint8_t HmmIsLivePayload(const void *ptr);
HMM_NO_ASAN void HmmCoalesceAndInsert(block_header_t *block);

/* Core heap growth helper using real sbrk */
HMM_NO_ASAN block_header_t *GrowHeap(size_t total_bytes);

#endif /* HMM_INTERNAL_H */