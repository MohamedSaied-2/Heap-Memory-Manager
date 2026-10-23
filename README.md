# Heap Memory Manager (HMM) - Architecture & Design Specification

## 1. Project Overview & Structure

The **Heap Memory Manager (HMM)** is a lightweight, high-performance custom memory allocator designed to replace standard library memory management routines (`malloc`, `free`, `calloc`, `realloc`) via wrapper redirection. It implements an **Explicit Free List** combined with **Boundary Tags**, **First-Fit search strategy**, **Threshold-Based Splitting**, and **Immediate Coalescing** backed by a pre-reserved memory pool managed via a simulated program break (`g_heap_end`) to minimize OS system call overhead.

### Project Directory Structure

- `inc/Hmm.h`: Public interface, macro configurations (`ALIGNMENT`, `ALIGN`, `MIN_BLOCK_SIZE`, `SPLIT_THRESHOLD`, `HMM_HEADER_SIZE` and the payload/header conversion macros), and core metadata structures (`block_header_t` with union payload overlap, `block_footer_t`).
- `inc/HmmInternal.h`: Shared internal module states, global pointers (`g_free_list_head`, heap bounds), and helper function prototypes.
- `src/HmmAlloc.c`: Core allocation engine containing the boundary-tag and free-list helpers (`HmmSetTags`, `HmmFreeListInsert`, `HmmFreeListRemove`), lazy initialization and heap growth (`GrowHeap`), `HmmAlloc`, `hmmCalloc`, `hmmReAlloc`, and `hmmSize`.
- `src/HmmFree.c`: Deallocation engine containing pointer validation (`HmmIsLivePayload`), immediate coalescing (`HmmCoalesceAndInsert`), and simulated program break reduction (shrinking).
- `src/heapWrappers.c`: Standard system function name interceptors routing calls to our custom HMM engine.
- `test/main.c`: Instructor-provided test suite validating functional correctness, memory reuse, and program break consistency (`sbrk(0)`).

---

## 2. Core Architectural Design

### Boundary Tags & Union Layout

To achieve O(1) bidirectional traversal during coalescing while avoiding a dedicated link area on allocated blocks, `block_header_t` utilizes a C **`union`**:

- When a block is **free**, it utilizes space inside the union for explicit doubly-linked list pointers (`next` and `prev`).
- When a block is **allocated**, the user payload data is written directly over that exact same memory space, so the link pointers cost no additional space.
- Every block is bracketed by a header and a footer (`block_footer_t`), allowing the allocator to inspect adjacent physical neighbors instantly.

### Block Layout

|                       Field                           |       Size        |                          Notes                             |
|-------------------------------------------------------|-------------------|------------------------------------------------------------|
| **Header Metadata** (`size`, `is_free`, padding)      | 16 bytes          | Equals `HMM_HEADER_SIZE` (`offsetof(block_header_t, as)`)  |
| **Union Area** (`next`/`prev` links, or user payload) | at least 16 bytes | The pointer returned to the user starts here               |
| **Footer** (`size`, `is_free`, padding)               | 16 bytes          | Mirrors the header for backward traversal                  |

- Every allocated block carries a fixed **32 bytes** of metadata (header metadata + footer).
- `ALIGNMENT` is **16 bytes**, matching the x86-64 `malloc` guarantee. Every block size and every returned pointer is a multiple of 16.
- Total block size for a request is `ALIGN(size) + HMM_HEADER_SIZE + sizeof(block_footer_t)`.

---

## 3. Minimum Valid Block Size and Split Threshold

To prevent the creation of useless memory fragments ("splinters") that are too small to ever fulfill future allocations, splitting is regulated by a threshold derived from the block layout.

**Minimum Block Size (`MIN_BLOCK_SIZE`) = Header Metadata + Links + Footer = 16 + 16 + 16 = 48 bytes**

Where:

- Header metadata is 16 bytes (`HMM_HEADER_SIZE`).
- The `next`/`prev` links occupy 16 bytes inside the union, in the same space the user payload uses when the block is allocated. They are counted once, not in addition to the payload.
- The footer is 16 bytes.
- A minimum block can therefore also serve a request of up to 16 bytes of payload (one `ALIGNMENT` unit).

**Split Threshold (`SPLIT_THRESHOLD`) = `MIN_BLOCK_SIZE` + 16 = 64 bytes**

The extra 16 bytes is a fixed margin of one alignment unit, so a split-off remainder always has at least 32 bytes of usable payload rather than merely the bare minimum.

**Splitting Rule:**

A free block of size `S_block` requested for an allocation of total size `S_needed` will only be split if the remainder satisfies:

**Remainder = S_block - S_needed**

**Remainder >= SPLIT_THRESHOLD**

If the remainder is smaller than `SPLIT_THRESHOLD`, splitting is bypassed and the entire block is allocated (absorbing the sliver as internal padding), preventing list pollution. The same rule applies when `hmmReAlloc` shrinks a block in place.

---

## 4. Algorithmic Strategy: First-Fit + Threshold Splitting

### Why First-Fit?

First-fit scans the explicit free list from the head and selects the first block that satisfies the request. Freed blocks are inserted at the head of the list, so recently freed blocks are found first, exhibiting strong cache locality.

### Why Threshold Splitting Reduces Fragmentation

- **Mitigating Micro-Splinters:** Naive allocators split blocks on every tiny size difference, populating the free list with hundreds of unusable tiny blocks. Threshold splitting ensures blocks are only subdivided if the leftover chunk can genuinely serve future allocations.
- **Balancing Internal vs. External Fragmentation:** By absorbing minor remainders as internal padding when below threshold, we keep the free list compact and search times low without causing noticeable internal waste.

### Heap Growth and Shrinking

- **Growth:** When no free block fits, `GrowHeap` checks against a pre-reserved memory pool. On the first allocation, it performs a single large reservation using `sbrk()`, and subsequent growths advance the simulated program break (`g_heap_end`) purely in software. If the last block in the heap is free, its space is reused without expanding the break.
- **Shrinking:** After coalescing, if the resulting free block sits at the top of the simulated heap boundary, the simulated program break (`g_heap_end`) is retreated downward directly in software without any negative `sbrk()` system calls, avoiding OS overhead.

### Pros and Cons of Our Design

**Pros:**

- High execution speed (O(N) free list traversal with frequent hits at the head).
- Immediate coalescing prevents external fragmentation buildup over time.
- Simulated heap shrinking via a software-managed program break satisfies footprint requirements while completely eliminating runtime system call overhead.
- Safe pointer validation protects against double-free, foreign-pointer, and corruption bugs: invalid pointers passed to `free` are ignored.

**Cons:**

- Explicit free list traversal can degrade to O(N) linear time under heavy allocation/deallocation churn compared to segregated free lists (bins). However, for university-grade workloads and standard benchmarks, explicit lists offer the optimal balance of simplicity and robustness.
- Shrinking only reclaims free space at the top of the heap. Free blocks in the middle of the heap are reused but never returned to the OS.

---

## 5. Summary of Functions

- `HmmAlloc(size_t size)`: Core First-Fit allocation engine with lazy initialization (on first growth) and threshold splitting. Returns `NULL` for a size of 0.
- `HmmFree(void *ptr)`: Safe deallocation engine with immediate bidirectional coalescing and simulated top-of-heap breaking.
- `hmmCalloc(size_t nmemb, size_t size)`: Zero-initialized allocation wrapper with multiplication overflow protection.
- `hmmReAlloc(void *ptr, size_t reqSize)`: Resizing engine. Shrinking is done in place (the unused tail is split off and coalesced when it meets the split threshold). Growing allocates a new block, copies the payload, and frees the old block.
- `hmmSize(void *ptr)`: Queries usable payload length of a live block.