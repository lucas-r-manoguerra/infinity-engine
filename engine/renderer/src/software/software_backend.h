// infinity/renderer/src/software/software_backend.h
//
// Software rasterizer backend (F4.2, ADR-004). Internal implementation of the
// Renderer interface: private to src/, never part of the public API, selected
// at compile time by createRenderer(). It rasterizes BGRA32 CPU render targets
// with a per-tile half-space scan (F4.4) in linear space, converting to sRGB
// exactly once per written pixel (F4.7, ADR-037).
//
//   Determinism - The renderer accumulates triangles in a reserved scratch
//                 buffer and, at present(), splits the target into disjoint
//                 square tiles. Tiles share nothing but read-only inputs, so
//                 the threaded path (thread pool, F4.4) and the serial path
//                 produce byte-identical framebuffers (rule 11).
//   Memory      - Every buffer comes from the injected core::Allocator
//                 (ADR-005) and is returned to it with its exact allocation
//                 size: the thread pool (when threaded), the triangle scratch,
//                 and the tile-job buffer. Growth only happens outside the
//                 raster loop and reports ALLOCATION_FAILED instead of
//                 crashing (rules 03/04/08). Internal allocations use
//                 alignment <= 8 and 8-aligned sizes so a byte-bump backend
//                 never hands back misaligned blocks.
#pragma once

#include "infinity/core/allocator.h"
#include "infinity/core/thread_pool.h"
#include "infinity/renderer/color.h"
#include "infinity/renderer/draw_list.h"
#include "infinity/renderer/error.h"
#include "infinity/renderer/render_target.h"
#include "infinity/renderer/renderer.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace infinity::renderer {

class SoftwareBackend final : public Renderer {
public:
    // pool is the thread pool used by the threaded tile path; createRenderer()
    // creates it and passes it in (nullptr for serial rendering). allocator
    // must outlive the backend and owns every buffer the backend allocates.
    SoftwareBackend(const RendererConfig& config, core::Allocator& allocator,
                    core::ThreadPool* pool) noexcept;
    ~SoftwareBackend() override;

    SoftwareBackend(const SoftwareBackend&) = delete;
    SoftwareBackend& operator=(const SoftwareBackend&) = delete;

    ExpectedVoid clear(const Color& color) noexcept override;
    ExpectedVoid draw(const DrawList& list) noexcept override;
    ExpectedVoid present(RenderTarget& target) noexcept override;
    [[nodiscard]] std::string_view backendName() const noexcept override;

private:
    // One submitted tile: which backend, which tile index. Jobs live in the
    // preallocated m_tileJobs buffer; present() waits for every tile before
    // returning, so a job's storage stays valid for its whole run.
    struct TileJob {
        SoftwareBackend* backend{nullptr};
        std::uint32_t tileIndex{0};
    };

    static void tileTask(void* userData) noexcept;

    ExpectedVoid reserveVertexSlots(std::size_t needed) noexcept;
    ExpectedVoid reserveTileJobs(std::size_t tiles) noexcept;
    void rasterizeTile(std::uint32_t tileIndex) noexcept;
    void clearTarget(RenderTarget& target) noexcept;
    void releaseBuffers() noexcept;

    core::Allocator* m_allocator{nullptr};
    core::ThreadPool* m_pool{nullptr};
    std::uint32_t m_tileSize{1};
    bool m_cullBackfaces{true};
    bool m_threaded{true};
    bool m_autoClear{true};
    Color m_clearColor{};

    // Frame-scoped triangle scratch: vertices accumulated by draw() since the
    // last present(). Grown on demand, released in the destructor.
    Vertex* m_vertices{nullptr};
    std::size_t m_vertexCapacity{0};
    std::size_t m_vertexCount{0};

    // Tile-job scratch, grown when a present() needs more tiles.
    TileJob* m_tileJobs{nullptr};
    std::size_t m_tileJobCapacity{0};

    // Transient state set at the start of present() and read by every tile.
    std::uint32_t* m_targetPixels{nullptr};
    std::uint32_t m_targetWidth{0};
    std::uint32_t m_targetHeight{0};
    std::uint32_t m_tileCols{0};
    std::uint32_t m_tileRows{0};
};

} // namespace infinity::renderer
