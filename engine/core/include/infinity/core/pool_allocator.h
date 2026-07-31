// infinity/core/pool_allocator.h
//
// PoolAllocator (F2.2, ADR-005, rules 03/08): a fixed-size element pool for
// ECS component storage. Every slot is allocated up front, so after
// construction there are zero allocations on any path — the ECS hot-path
// budget (rule 08).
//
//   Block        - ONE contiguous block of slotSize * count bytes is acquired
//                  from the backing Allocator at construction and never touched
//                  again. slotSize is elementSize rounded up to a multiple of
//                  the alignment (and to at least sizeof(void*) so the
//                  intrusive free list fits). The block is requested aligned to
//                  the pool alignment, so every slot starts at a correctly
//                  aligned address.
//   Free list    - Intrusive and O(1): the next-slot pointer of every free slot
//                  lives INSIDE the slot. The list is chained once at
//                  construction over the whole block, so it adds zero extra
//                  allocation. Order is deterministic (rule 11): slots are
//                  chained in construction order, so allocate() hands out slots
//                  in a fixed, reproducible sequence.
//   Reuse        - deallocate() pushes the slot back onto the head of the free
//                  list (O(1)); a freed slot is the next one handed out.
//   Failure      - Exhaustion returns nullptr (recoverable, ADR-003). When the
//                  backing cannot provide the block at construction the pool is
//                  EMPTY: capacity() is 0 and every allocate() returns nullptr.
//   Constraints  - allocate() only serves the size/alignment pair it was built
//                  with: size must equal elementSize and alignment must equal
//                  the pool's alignment. A mismatch is a programming error
//                  (asserted in debug, ADR-003; release proceeds anyway).
//                  elementSize < alignment is not supported (asserted in debug;
//                  release clamps the slot stride). A size of 0 yields an empty
//                  pool without touching the backing.
//
// Lifetime: the backing Allocator must outlive the pool. Copy and move are
// deleted: the pool owns its block and is non-relocatable.
#pragma once

#include "infinity/core/allocator.h"

#include <cstddef>

namespace infinity::core {

// Fixed-size element pool serving slots of a single elementSize/alignment pair.
class PoolAllocator final : public Allocator {
public:
    // Pre-allocates count slots of elementSize bytes aligned to alignment from
    // backing (one block acquisition). backing must outlive the pool. When the
    // acquisition fails the pool is empty (see class docs).
    PoolAllocator(size_t elementSize, size_t alignment, size_t count, Allocator& backing) noexcept;

    // Convenience: same contract backed by an internal malloc-based backend,
    // so the pool is usable standalone. malloc stays inside that backend
    // (rule 03); it never appears in headers or business code.
    PoolAllocator(size_t elementSize, size_t alignment, size_t count) noexcept;

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    PoolAllocator(PoolAllocator&&) noexcept = delete;
    PoolAllocator& operator=(PoolAllocator&&) noexcept = delete;

    // Returns the block to the backing. RAII (rule 03).
    ~PoolAllocator() override;

    // Hands out the next free slot in deterministic construction order, or
    // nullptr when the pool is exhausted. O(1). size must equal elementSize and
    // alignment must equal the pool's alignment (asserted, ADR-003; a mismatch
    // in release is served from the free list anyway).
    [[nodiscard]] void* allocate(size_t size, size_t alignment) noexcept override;

    // Returns a slot previously obtained from allocate() to the free list.
    // O(1). size must equal elementSize (asserted, ADR-003). Passing nullptr is
    // a no-op; deallocating any other pointer is undefined behavior.
    void deallocate(void* ptr, size_t size) noexcept override;

    // True only for the pool's own alignment, and for alignment 1 when the pool
    // is non-empty (any slot satisfies a byte-aligned request); false otherwise,
    // including invalid alignments and an empty pool.
    [[nodiscard]] bool supportsAlignment(size_t alignment) const noexcept override;

    [[nodiscard]] size_t capacity() const noexcept;
    [[nodiscard]] size_t usedCount() const noexcept;

private:
    // Acquires the block from m_backing; leaves the pool empty on failure.
    void acquireBlock() noexcept;

    // Chains every slot into the free list in construction order.
    void initFreeList() noexcept;

    Allocator& m_backing;      // must outlive the pool
    void* m_block{nullptr};    // contiguous block from m_backing
    void* m_freeHead{nullptr}; // next slot to hand out, or nullptr
    size_t m_elementSize{0};   // the only size allocate() may serve
    size_t m_alignment{1};     // the only alignment the pool serves
    size_t m_capacity{0};      // slot count; 0 when the block was not acquired
    size_t m_usedCount{0};     // slots currently handed out
    size_t m_slotSize{0};      // stride between slots (>= alignment)
    size_t m_blockSize{0};     // bytes requested from the backing
};

} // namespace infinity::core
