// tests/renderer/software_raster_test.cpp
//
// Contract tests for the software renderer rasterization path (F4.3, ADR-037):
// clear/draw/present work against BGRA32 render targets in linear space with
// sRGB applied at present, and backface culling is screen-space and
// configurable (on by default, off draws backfaces). Exact pixel expectations,
// never "looks right" (rule 06). Factory cases live in
// software_factory_test.cpp, determinism cases in software_determinism_test.cpp
// (rule 01: One File = One Task).
#include "infinity/renderer/color.h"
#include "infinity/renderer/draw_list.h"
#include "infinity/renderer/render_target.h"
#include "infinity/renderer/renderer.h"

#include "infinity/core/allocator.h"
#include "infinity/core/error.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string_view>

#include <doctest/doctest.h>

namespace {

using infinity::renderer::Color;
using infinity::renderer::createRenderer;
using infinity::renderer::createRenderTarget;
using infinity::renderer::DrawList;
using infinity::renderer::linearToSrgb;
using infinity::renderer::RendererConfig;
using infinity::renderer::RenderTarget;
using infinity::renderer::Vertex;

// Minimal Allocator double (same as render_target_test.cpp).
class BumpAllocator final : public infinity::core::Allocator {
public:
    explicit BumpAllocator(size_t budget) noexcept : m_budget(budget) {}

    [[nodiscard]] void* allocate(size_t size, size_t alignment) noexcept override {
        if (!infinity::core::isValidAlignment(alignment) || m_used + size > m_budget) {
            return nullptr;
        }
        void* block = m_storage.data() + m_used;
        m_used += size;
        ++m_allocationCount;
        return block;
    }

    void deallocate(void* ptr, size_t size) noexcept override {
        if (ptr == nullptr) {
            return;
        }
        m_freedBytes += size;
    }

    [[nodiscard]] bool supportsAlignment(size_t alignment) const noexcept override {
        return infinity::core::isValidAlignment(alignment);
    }

    [[nodiscard]] size_t usedBytes() const noexcept { return m_used; }
    [[nodiscard]] size_t allocationCount() const noexcept { return m_allocationCount; }
    [[nodiscard]] size_t freedBytes() const noexcept { return m_freedBytes; }

private:
    alignas(16) std::array<unsigned char, 65536> m_storage{};
    size_t m_used{0};
    size_t m_allocationCount{0};
    size_t m_freedBytes{0};
    size_t m_budget{0};
};

// Packs linear RGBA into the BGRA32 wire format (bytes B,G,R,A in memory).
// Rounding rule (contract): truncation after +0.5.
[[nodiscard]] std::uint32_t packBgra(const Color& linearColor) noexcept {
    const Color srgb = linearToSrgb(linearColor);
    const auto byte = [](float channel) {
        // NOLINTNEXTLINE(bugprone-incorrect-roundings) -- contract: +0.5 then truncate.
        return static_cast<std::uint32_t>(0.5f + (255.0f * channel));
    };
    const std::uint32_t a = byte(srgb.a);
    const std::uint32_t r = byte(srgb.r);
    const std::uint32_t g = byte(srgb.g);
    const std::uint32_t b = byte(srgb.b);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

// Renders one frame into target: clear, draw the given vertices, present.
// Returns the result of present so tests can assert it.
[[nodiscard]] infinity::renderer::ExpectedVoid
renderFrame(infinity::renderer::Renderer& renderer, RenderTarget& target, const Color& clearColor,
            const std::span<const Vertex>& vertices) {
    const auto clear = renderer.clear(clearColor);
    if (!clear.has_value()) {
        return clear;
    }
    const DrawList list{.vertices = vertices};
    const auto drawn = renderer.draw(list);
    if (!drawn.has_value()) {
        return drawn;
    }
    return renderer.present(target);
}

constexpr Color BLACK{.r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f};
constexpr Color WHITE{.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f};

} // namespace

TEST_CASE("clear then present fills the target with the clear color") {
    BumpAllocator allocator{65536};
    RendererConfig config;
    config.allocator = &allocator;

    const auto renderer = createRenderer(config);
    auto target = createRenderTarget(4, 4, allocator);
    CHECK(renderer.has_value());
    CHECK(target.has_value());
    if (!renderer.has_value() || !target.has_value()) {
        return;
    }

    const auto present = renderFrame(**renderer, *target, BLACK, std::span<const Vertex>{});
    CHECK(present.has_value());
    const std::uint32_t expectedBlack = packBgra(BLACK);
    for (const std::uint32_t pixel : (*target).pixels()) {
        CHECK(pixel == expectedBlack);
    }

    const Color navy{.r = 0.2f, .g = 0.4f, .b = 0.6f, .a = 1.0f};
    CHECK(renderFrame(**renderer, *target, navy, std::span<const Vertex>{}).has_value());
    const std::uint32_t expectedNavy = packBgra(navy);
    for (const std::uint32_t pixel : (*target).pixels()) {
        CHECK(pixel == expectedNavy);
    }
}

TEST_CASE("a front-facing triangle fills its interior pixels") {
    BumpAllocator allocator{65536};
    RendererConfig config;
    config.allocator = &allocator;

    const auto renderer = createRenderer(config);
    auto target = createRenderTarget(4, 4, allocator);
    CHECK(renderer.has_value());
    CHECK(target.has_value());
    if (!renderer.has_value() || !target.has_value()) {
        return;
    }

    // Screen-space winding (y down): positive signed area = front-facing. This
    // triangle covers the top-left half of the 4x4 target.
    const std::array<Vertex, 3> triangle{
        Vertex{.x = 0.0f, .y = 0.0f, .color = WHITE},
        Vertex{.x = 4.0f, .y = 0.0f, .color = WHITE},
        Vertex{.x = 0.0f, .y = 4.0f, .color = WHITE},
    };

    const auto present = renderFrame(**renderer, *target, BLACK, triangle);
    CHECK(present.has_value());

    // Pixel (1,1) lies inside the triangle and is fully white (opaque).
    CHECK((*target).pixels()[(1u * 4u) + 1u] == 0xFFFFFFFFu);
    // Pixel (3,3) lies outside the triangle and keeps the clear color.
    CHECK((*target).pixels()[(3u * 4u) + 3u] == packBgra(BLACK));
}

TEST_CASE("a backfacing triangle is culled by default") {
    BumpAllocator allocator{65536};
    RendererConfig config;
    config.allocator = &allocator;

    const auto renderer = createRenderer(config);
    auto target = createRenderTarget(4, 4, allocator);
    CHECK(renderer.has_value());
    CHECK(target.has_value());
    if (!renderer.has_value() || !target.has_value()) {
        return;
    }

    // Same geometric region as the front-facing test, reversed winding.
    const std::array<Vertex, 3> triangle{
        Vertex{.x = 0.0f, .y = 0.0f, .color = WHITE},
        Vertex{.x = 0.0f, .y = 4.0f, .color = WHITE},
        Vertex{.x = 4.0f, .y = 0.0f, .color = WHITE},
    };

    const auto present = renderFrame(**renderer, *target, BLACK, triangle);
    CHECK(present.has_value());

    const std::uint32_t clearPixel = packBgra(BLACK);
    for (const std::uint32_t pixel : (*target).pixels()) {
        CHECK(pixel == clearPixel);
    }
}

TEST_CASE("a backfacing triangle draws when culling is disabled") {
    BumpAllocator allocator{65536};
    RendererConfig config;
    config.allocator = &allocator;
    config.cullBackfaces = false;

    const auto renderer = createRenderer(config);
    auto target = createRenderTarget(4, 4, allocator);
    CHECK(renderer.has_value());
    CHECK(target.has_value());
    if (!renderer.has_value() || !target.has_value()) {
        return;
    }

    const std::array<Vertex, 3> triangle{
        Vertex{.x = 0.0f, .y = 0.0f, .color = WHITE},
        Vertex{.x = 0.0f, .y = 4.0f, .color = WHITE},
        Vertex{.x = 4.0f, .y = 0.0f, .color = WHITE},
    };

    const auto present = renderFrame(**renderer, *target, BLACK, triangle);
    CHECK(present.has_value());

    CHECK((*target).pixels()[(1u * 4u) + 1u] == 0xFFFFFFFFu);
}
