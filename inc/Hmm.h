/*
    File : Hmm.h
    Desc : Heap Memory Manager include file
*/

#ifndef HMM_H
#define HMM_H

#include <stddef.h>
#include <stdint.h>

// ==========================================
// 1. MACROS & ALIGNMENT CONFIGURATION
// ==========================================

// System alignment boundary
#define ALIGNMENT 8

// Round up any given size to the nearest multiple of ALIGNMENT
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

// Minimum block size: Header + Footer must accommodate metadata and free pointers
#define MIN_BLOCK_SIZE ALIGN(sizeof(block_header_t) + sizeof(block_footer_t))

// Threshold for splitting
#define SPLIT_THRESHOLD (MIN_BLOCK_SIZE + 16)


// ==========================================
// 2. METADATA STRUCTURES (BOUNDARY TAGS & UNION)
// ==========================================

typedef struct block_header block_header_t;

struct block_header {
    size_t size;                // Total size of the block (including header and footer)
    uint8_t is_free;            // Status flag: 1 if free, 0 if allocated
    uint8_t padding[7];         // Explicit padding to maintain strict 8-byte alignment
    
    // When the block is free, these pointers use this space.
    // When the block is allocated, the user writes their data directly over this space.
    union {
        struct {
            block_header_t *next;   // Next free block in explicit list
            block_header_t *prev;   // Previous free block in explicit list
        } free_links;
        
        char payload_placeholder[1]; // Marks the start of the user payload area
    } as;
};

typedef struct block_footer {
    size_t size;                // Mirrors the header size
    uint8_t is_free;            // Mirrors the allocation status
    uint8_t padding[7];         // Explicit padding for alignment consistency
} block_footer_t;

#define HMM_HEADER_SIZE ((size_t)offsetof(block_header_t, as))
#define HMM_TOTAL_SIZE(size) (ALIGN(size) + HMM_HEADER_SIZE + sizeof(block_footer_t))
#define HMM_PAYLOAD(block) ((void *)((uint8_t *)(block) + HMM_HEADER_SIZE))
#define HMM_BLOCK_OF(ptr) ((block_header_t *)((uint8_t *)(ptr) - HMM_HEADER_SIZE))


// ==========================================
// 3. PUBLIC API FUNCTION PROTOTYPES
// ==========================================

void HmmFree(void *ptr);

void *HmmAlloc(size_t size);

void *hmmCalloc(size_t nmemb, size_t size);

void *hmmReAlloc(void *ptr, size_t reqSize);

size_t hmmSize(void *ptr);

#endif /* HMM_H */