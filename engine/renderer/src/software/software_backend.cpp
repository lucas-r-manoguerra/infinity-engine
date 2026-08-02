// infinity/renderer/src/software/software_backend.cpp
#include "software/software_backend.h"

#include "infinity/renderer/color.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace infinity::renderer {

namespace {
// Initial frame-scoped scratch: 64 triangles. Grown on demand, never in the
// raster loop (rules 03/08); growth failure surfaces as ALLOCATION_FAILED.
constexpr std::size_t INITIAL_VERTEX_CAPACITY = 192;
// Initial tile-job buffer: covers a 8x8 tile grid (e.g. 256x256 @ tile 32).
constexpr std::size_t INITIAL_TILE_CAPACITY = 64;

// Packs a linear color into BGRA32 after a single linear-to-sRGB conversion
// (ADR-037). RGB goes through the precomputed 1024-entry lookup table (G2)
// instead of a per-channel std::pow; alpha is packed without a gamma curve.
// Rounding is +0.5 then truncate, matching the test oracle; bytes are
// B,G,R,A in memory (little-endian uint32).
[[nodiscard]] std::uint32_t packSrgb(const Color& linearColor) noexcept {
    const std::array<std::uint8_t, 1024>& lut = srgbLookupTable();
    const auto byte = [](float channel) {
        // NOLINTNEXTLINE(bugprone-incorrect-roundings) -- contract: +0.5 then truncate.
        return static_cast<std::uint32_t>(0.5f + (255.0f * channel));
    };
    const auto lookup = [&lut](float linear) {
        const float clamped = std::clamp(linear, 0.0f, 1.0f);
        // NOLINTNEXTLINE(bugprone-incorrect-roundings) -- contract: +0.5 then truncate.
        const auto index = static_cast<std::size_t>(0.5f + (1023.0f * clamped));
        return static_cast<std::uint32_t>(lut[index]);
    };
    return (byte(linearColor.a) << 24) | (lookup(linearColor.r) << 16) |
           (lookup(linearColor.g) << 8) | lookup(linearColor.b);
}
} // namespace

SoftwareBackend::SoftwareBackend(const RendererConfig& config, core::Allocator& allocator,
                                 core::ThreadPool* pool) noexcept
    : m_allocator(&allocator), m_pool(pool), m_tileSize(config.tileSize == 0 ? 1 : config.tileSize),
      m_cullBackfaces(config.cullBackfaces), m_threaded(config.threaded),
      m_autoClear(config.autoClear) {}

SoftwareBackend::~SoftwareBackend() { releaseBuffers(); }

ExpectedVoid SoftwareBackend::clear(const Color& color) noexcept {
    m_clearColor = color;
    return {};
}

ExpectedVoid SoftwareBackend::draw(const DrawList& list) noexcept {
    const std::size_t count = list.vertices.size();
    if (count % 3 != 0) {
        return std::unexpected(RenderError::INVALID_ARGUMENT);
    }
    const std::size_t needed = m_vertexCount + count;
    if (needed > m_vertexCapacity) {
        ExpectedVoid grew = reserveVertexSlots(needed);
        if (!grew.has_value()) {
            return grew;
        }
    }
    std::copy_n(list.vertices.data(), count, m_vertices + m_vertexCount);
    m_vertexCount = needed;
    return {};
}

ExpectedVoid SoftwareBackend::present(RenderTarget& target) noexcept {
    if (target.width() == 0 || target.height() == 0) {
        return std::unexpected(RenderError::INVALID_ARGUMENT);
    }
    m_targetWidth = target.width();
    m_targetHeight = target.height();
    m_targetPixels = target.pixels().data();

    if (m_autoClear) {
        clearTarget(target);
    }
    m_tileCols = (m_targetWidth + m_tileSize - 1) / m_tileSize;
    m_tileRows = (m_targetHeight + m_tileSize - 1) / m_tileSize;
    const std::uint32_t tileCount = m_tileCols * m_tileRows;
    if (tileCount > m_tileJobCapacity) {
        ExpectedVoid grew = reserveTileJobs(tileCount);
        if (!grew.has_value()) {
            return grew;
        }
    }

    if (m_threaded && m_pool != nullptr) {
        for (std::uint32_t i = 0; i < tileCount; ++i) {
            m_tileJobs[i] = TileJob{.backend = this, .tileIndex = i};
            m_pool->submit(&SoftwareBackend::tileTask, &m_tileJobs[i]);
        }
        m_pool->waitAll();
    } else {
        for (std::uint32_t i = 0; i < tileCount; ++i) {
            rasterizeTile(i);
        }
    }

    // The frame is consumed: the next clear/draw starts a fresh scene.
    m_vertexCount = 0;
    return {};
}

std::string_view SoftwareBackend::backendName() const noexcept { return "software"; }

ExpectedVoid SoftwareBackend::reserveVertexSlots(std::size_t needed) noexcept {
    // The first reservation allocates the initial frame-scoped scratch so the
    // raster loop never allocates (rules 03/08); growth past it happens at draw
    // submission time, never per pixel, and surfaces as ALLOCATION_FAILED.
    const std::size_t capacity = std::max(INITIAL_VERTEX_CAPACITY, needed);
    const std::size_t bytes = capacity * sizeof(Vertex);
    auto* newBuffer = static_cast<Vertex*>(m_allocator->allocate(bytes, alignof(Vertex)));
    if (newBuffer == nullptr) {
        return std::unexpected(RenderError::ALLOCATION_FAILED);
    }
    if (m_vertices != nullptr) {
        std::copy_n(m_vertices, m_vertexCount, newBuffer);
        m_allocator->deallocate(m_vertices, m_vertexCapacity * sizeof(Vertex));
    }
    m_vertices = newBuffer;
    m_vertexCapacity = capacity;
    return {};
}

ExpectedVoid SoftwareBackend::reserveTileJobs(std::size_t tiles) noexcept {
    // Same discipline as the vertex scratch: floor the first tile-job buffer at
    // a fixed size and only grow when a frame exceeds it (never per tile).
    const std::size_t capacity = std::max(INITIAL_TILE_CAPACITY, tiles);
    const std::size_t bytes = capacity * sizeof(TileJob);
    auto* newJobs = static_cast<TileJob*>(m_allocator->allocate(bytes, alignof(TileJob)));
    if (newJobs == nullptr) {
        return std::unexpected(RenderError::ALLOCATION_FAILED);
    }
    if (m_tileJobs != nullptr) {
        m_allocator->deallocate(m_tileJobs, m_tileJobCapacity * sizeof(TileJob));
    }
    m_tileJobs = newJobs;
    m_tileJobCapacity = capacity;
    return {};
}

void SoftwareBackend::tileTask(void* userData) noexcept {
    const auto* job = static_cast<const TileJob*>(userData);
    job->backend->rasterizeTile(job->tileIndex);
}

void SoftwareBackend::clearTarget(RenderTarget& target) noexcept {
    const std::uint32_t value = packSrgb(m_clearColor);
    for (std::uint32_t& pixel : target.pixels()) {
        pixel = value;
    }
}

void SoftwareBackend::rasterizeTile(std::uint32_t tileIndex) noexcept {
    const std::uint32_t tileCol = tileIndex % m_tileCols;
    const std::uint32_t tileRow = tileIndex / m_tileCols;
    const std::uint32_t rectX0 = tileCol * m_tileSize;
    const std::uint32_t rectY0 = tileRow * m_tileSize;
    const std::uint32_t rectX1 = std::min(rectX0 + m_tileSize, m_targetWidth);
    const std::uint32_t rectY1 = std::min(rectY0 + m_tileSize, m_targetHeight);

    for (std::size_t t = 0; t < m_vertexCount; t += 3) {
        const Vertex& v0 = m_vertices[t];
        const Vertex& v1 = m_vertices[t + 1];
        const Vertex& v2 = m_vertices[t + 2];

        // Screen-space winding (y down): positive signed area is front-facing.
        const float area2 = ((v1.x - v0.x) * (v2.y - v0.y)) - ((v1.y - v0.y) * (v2.x - v0.x));
        if (area2 == 0.0f || (m_cullBackfaces && area2 < 0.0f)) {
            continue;
        }

        // Per-triangle bounding box clamped to this tile. Vertices may lie
        // outside the target, so the box is clamped before casting to int.
        const int bboxMinX = std::max(static_cast<int>(rectX0),
                                      static_cast<int>(std::floor(std::min({v0.x, v1.x, v2.x}))));
        const int bboxMaxX = std::min(static_cast<int>(rectX1),
                                      static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x}))));
        const int bboxMinY = std::max(static_cast<int>(rectY0),
                                      static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y}))));
        const int bboxMaxY = std::min(static_cast<int>(rectY1),
                                      static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y}))));

        for (int y = bboxMinY; y < bboxMaxY; ++y) {
            const float py = static_cast<float>(y) + 0.5f;
            for (int x = bboxMinX; x < bboxMaxX; ++x) {
                const float px = static_cast<float>(x) + 0.5f;
                // Half-space edge functions; a front-facing triangle contains
                // its sample point when every edge is >= 0, a back-facing one
                // (culling disabled) when every edge is <= 0.
                const float e0 = ((v1.x - v0.x) * (py - v0.y)) - ((v1.y - v0.y) * (px - v0.x));
                const float e1 = ((v2.x - v1.x) * (py - v1.y)) - ((v2.y - v1.y) * (px - v1.x));
                const float e2 = ((v0.x - v2.x) * (py - v2.y)) - ((v0.y - v2.y) * (px - v2.x));
                const bool front = area2 > 0.0f;
                const bool inside = front ? (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f)
                                          : (e0 <= 0.0f && e1 <= 0.0f && e2 <= 0.0f);
                if (!inside) {
                    continue;
                }

                // Barycentric weights: edge i is zero on the opposite edge and
                // equals area2 at its apex, so w = e / area2 works for either
                // winding.
                const float invArea = 1.0f / area2;
                const float w0 = e1 * invArea;
                const float w1 = e2 * invArea;
                const float w2 = e0 * invArea;
                const Color color{.r = (w0 * v0.color.r) + (w1 * v1.color.r) + (w2 * v2.color.r),
                                  .g = (w0 * v0.color.g) + (w1 * v1.color.g) + (w2 * v2.color.g),
                                  .b = (w0 * v0.color.b) + (w1 * v1.color.b) + (w2 * v2.color.b),
                                  .a = (w0 * v0.color.a) + (w1 * v1.color.a) + (w2 * v2.color.a)};
                m_targetPixels[(static_cast<std::size_t>(y) * m_targetWidth) +
                               static_cast<std::size_t>(x)] = packSrgb(color);
            }
        }
    }
}

void SoftwareBackend::releaseBuffers() noexcept {
    if (m_tileJobs != nullptr) {
        m_allocator->deallocate(m_tileJobs, m_tileJobCapacity * sizeof(TileJob));
        m_tileJobs = nullptr;
        m_tileJobCapacity = 0;
    }
    if (m_vertices != nullptr) {
        m_allocator->deallocate(m_vertices, m_vertexCapacity * sizeof(Vertex));
        m_vertices = nullptr;
        m_vertexCapacity = 0;
    }
    if (m_pool != nullptr) {
        m_pool->~ThreadPool();
        m_allocator->deallocate(m_pool, sizeof(core::ThreadPool));
        m_pool = nullptr;
    }
}

} // namespace infinity::renderer
