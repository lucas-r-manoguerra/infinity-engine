// src/pool_allocator.cpp
#include "infinity/core/pool_allocator.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace infinity::core {
namespace {

// Internal malloc-backed Allocator used by the standalone PoolAllocator
// constructor. Wraps std::malloc/std::free (rule 03: malloc lives only inside
// allocator implementations). Stateless: the C runtime owns all heap state, so
// a single program-scoped instance is safe to share, adds no hidden state
// (rule 11) and always outlives any pool.
class MallocAllocator final : public Allocator {
public:
    [[nodiscard]] void* allocate(size_t size, size_t alignment) noexcept override {
        if (!isValidAlignment(alignment) || alignment > alignof(std::max_align_t)) {
            return nullptr;
        }
        return std::malloc(size);
    }

    void deallocate(void* ptr, size_t size) noexcept override {
        (void)size;
        std::free(ptr);
    }

    [[nodiscard]] bool supportsAlignment(size_t alignment) const noexcept override {
        return isValidAlignment(alignment) && alignment <= alignof(std::max_align_t);
    }
};

// Program-scoped default backing for standalone pools. See MallocAllocator: it
// is stateless, so this is not hidden state.
Allocator& defaultBacking() noexcept {
    static MallocAllocator backing;
    return backing;
}

// Rounds value up to a multiple of alignment (a power of two).
[[nodiscard]] constexpr size_t alignUp(size_t value, size_t alignment) noexcept {
    const size_t mask = alignment - 1;
    return (value + mask) & ~mask;
}

} // namespace

PoolAllocator::PoolAllocator(size_t elementSize, size_t alignment, size_t count,
                             Allocator& backing) noexcept
    : m_backing(backing), m_elementSize(elementSize), m_alignment(alignment), m_capacity(count),
      m_slotSize(alignUp(std::max(elementSize, sizeof(void*)), alignment)),
      m_blockSize(count * m_slotSize) {
    assert(isValidAlignment(alignment));
    assert(elementSize >= alignment); // elementSize < alignment is not supported
    acquireBlock();
}

PoolAllocator::PoolAllocator(size_t elementSize, size_t alignment, size_t count) noexcept
    : PoolAllocator(elementSize, alignment, count, defaultBacking()) {}

PoolAllocator::~PoolAllocator() {
    if (m_block != nullptr) {
        m_backing.deallocate(m_block, m_blockSize);
    }
}

void* PoolAllocator::allocate(size_t size, size_t alignment) noexcept {
    assert(size == m_elementSize);
    assert(alignment == m_alignment);
    (void)size;
    (void)alignment;
    if (m_freeHead == nullptr) {
        return nullptr;
    }
    void* slot = m_freeHead;
    m_freeHead = *static_cast<void**>(slot);
    ++m_usedCount;
    return slot;
}

void PoolAllocator::deallocate(void* ptr, size_t size) noexcept {
    if (ptr == nullptr) {
        return;
    }
    assert(size == m_elementSize);
    assert(m_usedCount > 0);
    (void)size;
    *static_cast<void**>(ptr) = m_freeHead;
    m_freeHead = ptr;
    --m_usedCount;
}

bool PoolAllocator::supportsAlignment(size_t alignment) const noexcept {
    return isValidAlignment(alignment) && m_capacity > 0 &&
           (alignment == m_alignment || alignment == 1);
}

size_t PoolAllocator::capacity() const noexcept { return m_capacity; }

size_t PoolAllocator::usedCount() const noexcept { return m_usedCount; }

void PoolAllocator::acquireBlock() noexcept {
    if (m_capacity == 0) {
        return; // count == 0: empty pool, no backing traffic
    }
    m_block = m_backing.allocate(m_blockSize, m_alignment);
    if (m_block == nullptr) {
        m_capacity = 0;
        return;
    }
    assert(reinterpret_cast<uintptr_t>(m_block) % m_alignment == 0);
    initFreeList();
}

void PoolAllocator::initFreeList() noexcept {
    auto* block = static_cast<unsigned char*>(m_block);
    for (size_t i = 0; i < m_capacity; ++i) {
        void* slot = block + (i * m_slotSize);
        void* next = (i + 1 < m_capacity) ? block + ((i + 1) * m_slotSize) : nullptr;
        *static_cast<void**>(slot) = next;
    }
    m_freeHead = m_block;
}

} // namespace infinity::core
