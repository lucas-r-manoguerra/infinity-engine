// src/arena_allocator.cpp
#include "infinity/core/arena_allocator.h"

#include <cassert>
#include <cstddef>
#include <cstdlib>

namespace infinity::core {
namespace {

// Internal malloc-backed Allocator used by the standalone ArenaAllocator
// constructor. Wraps std::malloc/std::free (rule 03: malloc lives only inside
// allocator implementations). Stateless: the C runtime owns all heap state,
// so a single program-scoped instance is safe to share, adds no hidden state
// (rule 11) and always outlives any arena.
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

// Program-scoped default backing for standalone arenas. See MallocAllocator:
// it is stateless, so this is not hidden state.
Allocator& defaultBacking() noexcept {
    static MallocAllocator backing;
    return backing;
}

} // namespace

ArenaAllocator::ArenaAllocator(size_t capacityBytes, Allocator& backing) noexcept
    : m_backing(backing) {
    acquireBlock(capacityBytes);
}

ArenaAllocator::ArenaAllocator(size_t capacityBytes) noexcept : m_backing(defaultBacking()) {
    acquireBlock(capacityBytes);
}

ArenaAllocator::~ArenaAllocator() {
    if (m_block != nullptr) {
        m_backing.deallocate(m_block, m_capacity);
    }
}

void* ArenaAllocator::allocate(size_t size, size_t alignment) noexcept {
    assert(isValidAlignment(alignment));
    if (m_block == nullptr) {
        return nullptr;
    }
    const size_t alignmentMask = alignment - 1;
    const size_t alignedOffset = (m_offset + alignmentMask) & ~alignmentMask;
    if (alignedOffset > m_capacity || size > m_capacity - alignedOffset) {
        return nullptr;
    }
    void* result = static_cast<unsigned char*>(m_block) + alignedOffset;
    m_offset = alignedOffset + size;
    return result;
}

void ArenaAllocator::deallocate(void* ptr, size_t size) noexcept {
    (void)ptr;
    (void)size;
}

bool ArenaAllocator::supportsAlignment(size_t alignment) const noexcept {
    return isValidAlignment(alignment) && alignment <= m_blockAlignment;
}

void ArenaAllocator::reset() noexcept { m_offset = 0; }

size_t ArenaAllocator::usedBytes() const noexcept { return m_offset; }

size_t ArenaAllocator::capacityBytes() const noexcept { return m_capacity; }

size_t ArenaAllocator::maxAlignment() const noexcept { return m_blockAlignment; }

void ArenaAllocator::acquireBlock(size_t capacityBytes) noexcept {
    m_block = m_backing.allocate(capacityBytes, alignof(std::max_align_t));
    if (m_block != nullptr) {
        m_capacity = capacityBytes;
        m_blockAlignment = alignof(std::max_align_t);
    }
}

} // namespace infinity::core
