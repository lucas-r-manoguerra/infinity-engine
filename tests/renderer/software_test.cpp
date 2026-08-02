// tests/renderer/software_test.cpp
//
// Contract tests for the software renderer backend (F4.1-F4.4, F4.7, ADR-037):
// the Renderer interface is backend-agnostic and created through a factory,
// clear/draw/present work against BGRA32 render targets in linear space with
// sRGB applied at present, backface culling is screen-space and configurable,
// and rendering is deterministic (rule 11): the same frame produces the same
// framebuffer, and the multi-threaded tile path (F4.4) produces the exact same
// checksum as the single-threaded path. Exact checksums, never "looks right"
// (rule 06).
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

[[nodiscard]] bool isMappedTo(infinity::renderer::RenderError code,
                              infinity::core::ErrorCategory category) noexcept {
    return infinity::renderer::categoryOf(code) == category;
}

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

TEST_CASE("createRenderer returns a software renderer honoring the config") {
    BumpAllocator allocator{65536};
    RendererConfig config;
    config.allocator = &allocator;

    const auto result = createRenderer(config);

    CHECK(result.has_value());
    if (!result.has_value()) {
        return;
    }
    CHECK((*result).get() != nullptr);
    const std::string_view software = "software";
    CHECK((*result)->backendName() == software);
}

TEST_CASE("createRenderer rejects a null allocator") {
    const auto result = createRenderer(RendererConfig{});

    CHECK_FALSE(result.has_value());
    CHECK(isMappedTo(result.error(), infinity::core::ErrorCategory::INVALID_ARGUMENT));
}

TEST_CASE("createRenderer reports an allocation failure cleanly") {
    BumpAllocator allocator{4};
    RendererConfig config;
    config.allocator = &allocator;

    const auto result = createRenderer(config);

    CHECK_FALSE(result.has_value());
    CHECK(isMappedTo(result.error(), infinity::core::ErrorCategory::RESOURCE));
}

TEST_CASE("draw reports ALLOCATION_FAILED when the triangle buffer cannot grow") {
    // Budget covers the renderer object and the initial scratch reserve but not
    // growth past it; a scene larger than the preallocated capacity fails
    // cleanly instead of crashing (rule 08). If this platform's renderer object
    // is larger than the budget, createRenderer itself reports RESOURCE, which
    // is the same contract from the other side.
    BumpAllocator allocator{20000};
    RendererConfig config;
    config.allocator = &allocator;

    const auto renderer = createRenderer(config);
    if (!renderer.has_value()) {
        CHECK(isMappedTo(renderer.error(), infinity::core::ErrorCategory::RESOURCE));
        return;
    }

    std::array<Vertex, 900> vertices{};
    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].x = static_cast<float>(i % 16);
        vertices[i].y = static_cast<float>(i % 16);
        vertices[i].color = WHITE;
    }
    const DrawList list{.vertices = std::span<const Vertex>{vertices}};

    const auto drawn = (*renderer)->draw(list);
    CHECK_FALSE(drawn.has_value());
    CHECK(isMappedTo(drawn.error(), infinity::core::ErrorCategory::RESOURCE));
}

TEST_CASE("a large scene grows the triangle buffer and still renders") {
    BumpAllocator allocator{65536};
    RendererConfig config;
    config.allocator = &allocator;

    const auto renderer = createRenderer(config);
    auto target = createRenderTarget(64, 64, allocator);
    CHECK(renderer.has_value());
    CHECK(target.has_value());
    if (!renderer.has_value() || !target.has_value()) {
        return;
    }

    std::array<Vertex, 900> vertices{};
    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].x = static_cast<float>((i * 3) % 64);
        vertices[i].y = static_cast<float>((i * 7) % 64);
        vertices[i].color = WHITE;
    }

    const auto present = renderFrame(**renderer, *target, BLACK, vertices);
    CHECK(present.has_value());
    CHECK((*target).checksum() != 0);
}

TEST_CASE("draw with a vertex count not divisible by three is rejected") {
    BumpAllocator allocator{65536};
    RendererConfig config;
    config.allocator = &allocator;

    const auto renderer = createRenderer(config);
    CHECK(renderer.has_value());
    if (!renderer.has_value()) {
        return;
    }

    std::array<Vertex, 4> vertices{};
    const DrawList list{.vertices = std::span<const Vertex>{vertices}};

    const auto drawn = (*renderer)->draw(list);
    CHECK_FALSE(drawn.has_value());
    CHECK(isMappedTo(drawn.error(), infinity::core::ErrorCategory::INVALID_ARGUMENT));
}

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

TEST_CASE("the renderer is released to its allocator on destruction") {
    BumpAllocator allocator{65536};
    const size_t bytesBefore = allocator.usedBytes();

    {
        RendererConfig config;
        config.allocator = &allocator;
        const auto result = createRenderer(config);
        CHECK(result.has_value());
        if (!result.has_value()) {
            return;
        }
        CHECK(allocator.freedBytes() == 0);
    }

    // The deleter releases the backend block plus the scratch buffer it owns
    // with the exact allocation sizes (ADR-005): nothing leaks.
    CHECK(allocator.freedBytes() == allocator.usedBytes() - bytesBefore);
}
