// tests/renderer/software_factory_test.cpp
//
// Contract tests for the software renderer backend lifecycle (F4.1-F4.3,
// ADR-037): the Renderer interface is backend-agnostic and created through a
// factory that honors the config, rejects a null allocator, reports allocation
// failures cleanly, grows its triangle buffer, and releases everything to its
// allocator on destruction. Rasterization cases live in
// software_raster_test.cpp, determinism cases in software_determinism_test.cpp
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

[[nodiscard]] bool isMappedTo(infinity::renderer::RenderError code,
                              infinity::core::ErrorCategory category) noexcept {
    return infinity::renderer::categoryOf(code) == category;
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
