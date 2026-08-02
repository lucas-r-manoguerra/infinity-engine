// infinity/renderer/render_target.h
//
// RenderTarget (F4.1, ADR-041, rule 03). A CPU framebuffer the software
// backend rasterizes into: width x height pixels, 32-bit BGRA (bytes B,G,R,A
// in memory), owned by a core::Allocator.
//
//   Ownership - The buffer is allocated through the injector-provided
//               Allocator and returned to it on destruction with the exact
//               allocation size (ADR-005). The type is move-only: a target is
//               never copied, and a moved-from target owns nothing (width and
//               height are 0). Prefer createRenderTarget() over the direct
//               constructor, which exists only so the factory can build a
//               target over an already-allocated, zero-filled buffer.
//   Determinism - The buffer is zero-filled at creation and checksum() is a
//               stable FNV-1a-64 over the raw bytes, so equal frames always
//               yield equal checksums and unequal frames almost surely differ
//               (rule 11, F4.9). checksum() covers the memory layout, which is
//               little-endian BGRA by contract.
#pragma once

#include "infinity/core/allocator.h"
#include "infinity/renderer/error.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace infinity::renderer {

// CPU framebuffer in BGRA32. Move-only; see the file brief for ownership.
class RenderTarget {
public:
    RenderTarget() = delete;
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    // Constructs a target owning the given buffer (which must hold
    // width*height*4 bytes and be zero-filled). Ownership of the buffer
    // transfers to the target, which returns it to allocator on destruction.
    // Prefer createRenderTarget().
    RenderTarget(std::uint32_t width, std::uint32_t height, std::uint32_t* pixels,
                 core::Allocator& allocator) noexcept;

    // Transfer of ownership: the moved-from target becomes empty (width and
    // height 0, no buffer) and must not be used for rendering.
    RenderTarget(RenderTarget&& other) noexcept;
    RenderTarget& operator=(RenderTarget&& other) noexcept;

    // Returns the buffer to the owning allocator (RAII, rule 03). The moved-to
    // target releases the exact allocation size exactly once.
    ~RenderTarget();

    [[nodiscard]] std::uint32_t width() const noexcept { return m_width; }
    [[nodiscard]] std::uint32_t height() const noexcept { return m_height; }

    // Mutable access for the renderer to write into; const access for reading
    // (tests, checksums). The span covers width*height BGRA32 pixels.
    [[nodiscard]] std::span<std::uint32_t> pixels() noexcept {
        return std::span<std::uint32_t>{m_pixels, pixelCount()};
    }
    [[nodiscard]] std::span<const std::uint32_t> pixels() const noexcept {
        return std::span<const std::uint32_t>{m_pixels, pixelCount()};
    }

    // FNV-1a-64 over the raw bytes of the buffer (see the file brief).
    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    [[nodiscard]] std::size_t pixelCount() const noexcept {
        return static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    }

    core::Allocator* m_allocator{nullptr};
    std::uint32_t* m_pixels{nullptr};
    std::size_t m_byteCount{0};
    std::uint32_t m_width{0};
    std::uint32_t m_height{0};
};

// Creates a zero-filled target of the given size through allocator. Rejects a
// zero dimension or a byte count that overflows size_t with INVALID_SIZE, and
// reports ALLOCATION_FAILED when the allocator cannot satisfy the request.
[[nodiscard]] Expected<RenderTarget> createRenderTarget(std::uint32_t width, std::uint32_t height,
                                                        core::Allocator& allocator) noexcept;

} // namespace infinity::renderer
