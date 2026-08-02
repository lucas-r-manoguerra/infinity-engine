// tests/renderer/software_determinism_test.cpp
//
// Contract tests for software renderer determinism (F4.4, ADR-037, rule 11):
// rendering is reproducible — the same frame produces the same framebuffer,
// culling on/off does not change front-facing output, and the multi-threaded
// tile path produces the exact same checksum as the single-threaded path.
// Exact checksums, never "looks right" (rule 06). Factory cases live in
// software_factory_test.cpp, rasterization cases in software_raster_test.cpp
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

} // namespace

TEST_CASE("front-facing output is identical with and without culling") {
    BumpAllocator allocator{65536};

    // Distinct vertex colors make the interpolation non-trivial.
    const std::array<Vertex, 3> triangle{
        Vertex{.x = -2.0f, .y = -2.0f, .color = Color{.r = 1.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f}},
        Vertex{.x = 6.0f, .y = -2.0f, .color = Color{.r = 0.0f, .g = 1.0f, .b = 0.0f, .a = 1.0f}},
        Vertex{.x = -2.0f, .y = 6.0f, .color = Color{.r = 0.0f, .g = 0.0f, .b = 1.0f, .a = 1.0f}},
    };

    RendererConfig culled;
    culled.allocator = &allocator;
    culled.cullBackfaces = true;
    RendererConfig unculled;
    unculled.allocator = &allocator;
    unculled.cullBackfaces = false;

    const auto culledRenderer = createRenderer(culled);
    const auto unculledRenderer = createRenderer(unculled);
    auto culledTarget = createRenderTarget(4, 4, allocator);
    auto unculledTarget = createRenderTarget(4, 4, allocator);
    CHECK(culledRenderer.has_value());
    CHECK(unculledRenderer.has_value());
    CHECK(culledTarget.has_value());
    CHECK(unculledTarget.has_value());
    if (!culledRenderer.has_value() || !unculledRenderer.has_value() || !culledTarget.has_value() ||
        !unculledTarget.has_value()) {
        return;
    }

    CHECK(renderFrame(**culledRenderer, *culledTarget, BLACK, triangle).has_value());
    CHECK(renderFrame(**unculledRenderer, *unculledTarget, BLACK, triangle).has_value());

    CHECK((*culledTarget).checksum() == (*unculledTarget).checksum());
}

TEST_CASE("two identical frames produce identical checksums") {
    BumpAllocator allocator{65536};
    RendererConfig config;
    config.allocator = &allocator;

    const auto renderer = createRenderer(config);
    auto target = createRenderTarget(8, 8, allocator);
    CHECK(renderer.has_value());
    CHECK(target.has_value());
    if (!renderer.has_value() || !target.has_value()) {
        return;
    }

    const std::array<Vertex, 6> twoTriangles{
        Vertex{.x = 0.0f, .y = 0.0f, .color = Color{.r = 1.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f}},
        Vertex{.x = 8.0f, .y = 0.0f, .color = Color{.r = 0.0f, .g = 1.0f, .b = 0.0f, .a = 1.0f}},
        Vertex{.x = 0.0f, .y = 8.0f, .color = Color{.r = 0.0f, .g = 0.0f, .b = 1.0f, .a = 1.0f}},
        Vertex{.x = 8.0f, .y = 8.0f, .color = Color{.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f}},
        Vertex{.x = 0.0f, .y = 8.0f, .color = Color{.r = 1.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f}},
        Vertex{.x = 8.0f, .y = 0.0f, .color = Color{.r = 0.0f, .g = 1.0f, .b = 0.0f, .a = 1.0f}},
    };

    CHECK(renderFrame(**renderer, *target, BLACK, twoTriangles).has_value());
    const std::uint64_t firstChecksum = (*target).checksum();
    CHECK(renderFrame(**renderer, *target, BLACK, twoTriangles).has_value());
    const std::uint64_t secondChecksum = (*target).checksum();

    CHECK(firstChecksum == secondChecksum);
}

TEST_CASE("multi-threaded and single-threaded rendering produce identical checksums") {
    BumpAllocator allocator{65536};

    // Small tiles over a 16x16 target force many tiles: with threaded=true the
    // frame is rasterized across the thread pool, with threaded=false the exact
    // same tile code runs inline. Determinism (rule 11, F4.4) requires both
    // paths to produce the identical framebuffer.
    RendererConfig threaded;
    threaded.allocator = &allocator;
    threaded.tileSize = 2;
    threaded.threaded = true;
    RendererConfig serial;
    serial.allocator = &allocator;
    serial.tileSize = 2;
    serial.threaded = false;

    const auto threadedRenderer = createRenderer(threaded);
    const auto serialRenderer = createRenderer(serial);
    auto threadedTarget = createRenderTarget(16, 16, allocator);
    auto serialTarget = createRenderTarget(16, 16, allocator);
    CHECK(threadedRenderer.has_value());
    CHECK(serialRenderer.has_value());
    CHECK(threadedTarget.has_value());
    CHECK(serialTarget.has_value());
    if (!threadedRenderer.has_value() || !serialRenderer.has_value() ||
        !threadedTarget.has_value() || !serialTarget.has_value()) {
        return;
    }

    std::array<Vertex, 24> vertices{};
    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].x = static_cast<float>((i * 5) % 16);
        vertices[i].y = static_cast<float>((i * 3) % 16);
        vertices[i].color = Color{.r = 0.2f + (0.1f * static_cast<float>(i % 5)),
                                  .g = 0.3f + (0.1f * static_cast<float>(i % 4)),
                                  .b = 0.4f + (0.1f * static_cast<float>(i % 3)),
                                  .a = 1.0f};
    }

    CHECK(renderFrame(**threadedRenderer, *threadedTarget, BLACK, vertices).has_value());
    CHECK(renderFrame(**serialRenderer, *serialTarget, BLACK, vertices).has_value());

    CHECK((*threadedTarget).checksum() == (*serialTarget).checksum());
}
