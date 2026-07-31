// infinity/core/arena_allocator.h
//
// ArenaAllocator (F2.2, ADR-005, rules 03/08): a frame-scoped bump allocator.
// The engine's frame arena is reset every frame; after init the hot path
// allocates from the arena with no backing traffic.
//
//   Block        - One contiguous block is acquired ONCE from the backing
//                  Allocator at construction. Every request is served from
//                  this block by bumping an offset; no per-request call ever
//                  reaches the backing. Deterministic (rule 11): the same
//                  request sequence yields the same addresses.
//   Alignment    - Each request is aligned UP within the block. The block is
//                  aligned to alignof(std::max_align_t), so every alignment
//                  up to that is honored (see supportsAlignment).
//   Release      - deallocate() is a no-op: the arena releases everything at
//                  reset() or destruction. This invalidates every live
//                  allocation into the arena.
//   Failure      - Exhaustion returns nullptr (recoverable, ADR-003). When
//                  the backing cannot provide the block at construction, the
//                  arena is EMPTY: capacityBytes() is 0 and every allocate()
//                  returns nullptr.
//
// Lifetime: the backing Allocator must outlive the arena. Copy and move are
// deleted: a frame-scoped arena is non-relocatable.
#pragma once

#include "infinity/core/allocator.h"

#include <cstddef>

namespace infinity::core {

// Frame-scoped bump allocator over a block acquired from a backing Allocator.
class ArenaAllocator final : public Allocator {
public:
    // Acquires a block of capacityBytes from backing, aligned to
    // alignof(std::max_align_t). backing must outlive the arena. When the
    // acquisition fails the arena is empty (see class docs).
    ArenaAllocator(size_t capacityBytes, Allocator& backing) noexcept;

    // Convenience: same contract backed by an internal malloc-based backend,
    // so the arena is usable standalone. malloc stays inside that backend
    // (rule 03); it never appears in headers or business code.
    explicit ArenaAllocator(size_t capacityBytes) noexcept;

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;
    ArenaAllocator(ArenaAllocator&&) noexcept = delete;
    ArenaAllocator& operator=(ArenaAllocator&&) noexcept = delete;

    // Returns the block to the backing. RAII (rule 03).
    ~ArenaAllocator() override;

    // Bumps the offset up to alignment and hands out size bytes, or returns
    // nullptr when the block cannot satisfy the request. alignment must be a
    // power of two >= 1 (asserted, ADR-003); alignments above the block's are
    // the caller's responsibility via supportsAlignment.
    [[nodiscard]] void* allocate(size_t size, size_t alignment) noexcept override;

    // NO-OP: the arena releases everything at reset()/destruction. Passing
    // nullptr is also a no-op. Calling this does NOT make the block reusable.
    void deallocate(void* ptr, size_t size) noexcept override;

    // True for every valid alignment up to the block's alignment
    // (alignof(std::max_align_t)); false beyond it and for invalid values.
    [[nodiscard]] bool supportsAlignment(size_t alignment) const noexcept override;

    // Rewinds the bump offset to zero in O(1). ALL live allocations into the
    // arena become invalid.
    void reset() noexcept;

    [[nodiscard]] size_t usedBytes() const noexcept;
    [[nodiscard]] size_t capacityBytes() const noexcept;
    [[nodiscard]] size_t maxAlignment() const noexcept;

private:
    // Acquires the block from m_backing; leaves the arena empty on failure.
    void acquireBlock(size_t capacityBytes) noexcept;

    Allocator& m_backing;       // must outlive the arena
    void* m_block{nullptr};     // contiguous block from m_backing
    size_t m_capacity{0};       // block size; 0 when acquisition failed
    size_t m_offset{0};         // bump offset into m_block
    size_t m_blockAlignment{0}; // alignment of m_block (0 when empty)
};

} // namespace infinity::core
