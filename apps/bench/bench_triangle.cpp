// apps/bench/bench_triangle.cpp
//
// Release-mode microbenchmarks for the software renderer (F4.9, rule 08).
// Measures the full-frame cost (clear + draw + present) of a fixed triangle
// scene and derives the per-triangle and per-pixel cost against the F1
// baselines to be revised in F4.
//
// Design notes:
// - Self-contained: std::chrono only (via bench_harness.h), no third_party
//   dependency (ADR-061/068). Benchmarks measure, they never assert (rule 08).
//   Not a CTest target.
// - The renderer is configured single-threaded (threaded=false) so the number
//   reflects the raster hot path, not thread-pool scheduling; the threaded
//   path's correctness (identical checksum) is covered by the tests, rule 11.
// - The scene is fixed at 32 triangles over a 128x128 target, staying under
//   the reserved buffers so the measured loop never allocates (rules 03/08).
// - The checksum is fed into the volatile sink so the optimizer cannot
//   eliminate the clear/draw/present pipeline (bench_harness.h).
// - Output keeps the machine-parseable `bench:<name>:<ns_per_op>` format.
//
// Benchmarks measure, they never assert (rule 08). Not a CTest target.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "bench_harness.h"
#include "infinity/core/arena_allocator.h"
#include "infinity/renderer/color.h"
#include "infinity/renderer/draw_list.h"
#include "infinity/renderer/render_target.h"
#include "infinity/renderer/renderer.h"

using infinity::renderer::Color;
using infinity::renderer::createRenderer;
using infinity::renderer::createRenderTarget;
using infinity::renderer::DrawList;
using infinity::renderer::RendererConfig;
using infinity::renderer::Vertex;

// Entry point for the renderer suite. bench_math.cpp defines main() and calls
// this after the math and memory suites, so all three run under the single
// infinity-bench executable (an executable may have exactly one main()).
int runTriangleBenchmarks() {
    // Heap-backed arena (F2.2) as the renderer's allocator: benchmark-only
    // memory, released wholesale when the arena goes out of scope.
    infinity::core::ArenaAllocator arena(size_t{256} * 1024 * 1024);

    RendererConfig config;
    config.allocator = &arena;
    config.threaded = false;
    const auto renderer = createRenderer(config);
    auto target = createRenderTarget(128, 128, arena);
    if (!renderer.has_value() || !target.has_value()) {
        return 1;
    }

    // Fixed scene: 32 triangles spread over the 128x128 target with distinct
    // vertex colors, so rasterization interpolates non-trivially.
    constexpr size_t TRIANGLE_COUNT = 32;
    std::array<Vertex, TRIANGLE_COUNT * 3> vertices{};
    for (size_t t = 0; t < TRIANGLE_COUNT; ++t) {
        const auto cx = static_cast<float>((t * 37) % 128);
        const auto cy = static_cast<float>((t * 53) % 128);
        vertices[t * 3] =
            Vertex{.x = cx, .y = cy, .color = Color{.r = 1.0f, .g = 0.2f, .b = 0.1f, .a = 1.0f}};
        vertices[(t * 3) + 1] = Vertex{
            .x = cx + 16.0f, .y = cy, .color = Color{.r = 0.2f, .g = 1.0f, .b = 0.1f, .a = 1.0f}};
        vertices[(t * 3) + 2] = Vertex{
            .x = cx, .y = cy + 16.0f, .color = Color{.r = 0.1f, .g = 0.2f, .b = 1.0f, .a = 1.0f}};
    }
    const DrawList list{.vertices = std::span<const Vertex>{vertices}};
    const Color clearColor{.r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f};

    // Full frame: clear + draw + present. The returned checksum keeps the
    // pipeline observable through the harness sink; the loop never allocates
    // once buffers are reserved.
    const auto frame = bench::runBenchmark("renderer_triangle_frame", 100, 2000, [&](size_t) {
        const auto cleared = (*renderer)->clear(clearColor);
        const auto drawn = (*renderer)->draw(list);
        const auto presented = (*renderer)->present(*target);
        if (!cleared.has_value() || !drawn.has_value() || !presented.has_value()) {
            return 0.0f;
        }
        return static_cast<float>((*target).checksum() & 0xFFFFFFFFull);
    });
    bench::report(frame, "renderer.full_frame");
    bench::report({.name = "renderer_triangle",
                   .nsPerOp = frame.nsPerOp / static_cast<double>(TRIANGLE_COUNT)},
                  "renderer.triangle");

    // Per-pixel raster cost over the 128x128 target (16384 pixels/frame).
    const auto pixels = bench::runBenchmark("renderer_pixel", 100, 2000, [&](size_t) {
        const auto cleared = (*renderer)->clear(clearColor);
        const auto drawn = (*renderer)->draw(list);
        const auto presented = (*renderer)->present(*target);
        if (!cleared.has_value() || !drawn.has_value() || !presented.has_value()) {
            return 0.0f;
        }
        return static_cast<float>((*target).checksum() & 0xFFFFFFFFull);
    });
    bench::report(pixels, "renderer.pixel_frame");
    constexpr double PIXELS_PER_FRAME = 128.0 * 128.0;
    bench::report({.name = "renderer_pixel", .nsPerOp = pixels.nsPerOp / PIXELS_PER_FRAME},
                  "renderer.pixel");

    return 0;
}
