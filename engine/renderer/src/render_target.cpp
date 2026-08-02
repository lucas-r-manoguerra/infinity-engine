// infinity/renderer/src/render_target.cpp
#include "infinity/renderer/render_target.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace infinity::renderer {

namespace {
constexpr std::uint64_t FNV_OFFSET = 14695981039346656037ULL;
constexpr std::uint64_t FNV_PRIME = 1099511628211ULL;
} // namespace

RenderTarget::RenderTarget(std::uint32_t width, std::uint32_t height, std::uint32_t* pixels,
                           core::Allocator& allocator) noexcept
    : m_allocator(&allocator), m_pixels(pixels),
      m_byteCount(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                  sizeof(std::uint32_t)),
      m_width(width), m_height(height) {}

RenderTarget::RenderTarget(RenderTarget&& other) noexcept
    : m_allocator(other.m_allocator), m_pixels(other.m_pixels), m_byteCount(other.m_byteCount),
      m_width(other.m_width), m_height(other.m_height) {
    other.m_allocator = nullptr;
    other.m_pixels = nullptr;
    other.m_byteCount = 0;
    other.m_width = 0;
    other.m_height = 0;
}

RenderTarget& RenderTarget::operator=(RenderTarget&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (m_pixels != nullptr) {
        m_allocator->deallocate(m_pixels, m_byteCount);
    }
    m_allocator = other.m_allocator;
    m_pixels = other.m_pixels;
    m_byteCount = other.m_byteCount;
    m_width = other.m_width;
    m_height = other.m_height;
    other.m_allocator = nullptr;
    other.m_pixels = nullptr;
    other.m_byteCount = 0;
    other.m_width = 0;
    other.m_height = 0;
    return *this;
}

RenderTarget::~RenderTarget() {
    if (m_pixels != nullptr) {
        m_allocator->deallocate(m_pixels, m_byteCount);
    }
}

std::uint64_t RenderTarget::checksum() const noexcept {
    const auto* bytes = reinterpret_cast<const unsigned char*>(m_pixels);
    std::uint64_t hash = FNV_OFFSET;
    for (std::size_t i = 0; i < m_byteCount; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= FNV_PRIME;
    }
    return hash;
}

Expected<RenderTarget> createRenderTarget(std::uint32_t width, std::uint32_t height,
                                          core::Allocator& allocator) noexcept {
    if (width == 0 || height == 0) {
        return std::unexpected(RenderError::INVALID_SIZE);
    }
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixels > std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t)) {
        return std::unexpected(RenderError::INVALID_SIZE);
    }
    const std::size_t byteCount = static_cast<std::size_t>(pixels) * sizeof(std::uint32_t);
    auto* buffer =
        static_cast<std::uint32_t*>(allocator.allocate(byteCount, alignof(std::uint32_t)));
    if (buffer == nullptr) {
        return std::unexpected(RenderError::ALLOCATION_FAILED);
    }
    for (std::size_t i = 0; i < pixels; ++i) {
        buffer[i] = 0;
    }
    return RenderTarget(width, height, buffer, allocator);
}

} // namespace infinity::renderer
