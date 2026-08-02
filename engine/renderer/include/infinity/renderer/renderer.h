// infinity/renderer/renderer.h
//
// Renderer abstraction (F4.1, ADR-009, rules 01/03/08/11). The runtime and the
// apps talk to this interface only; concrete backends live in src/<backend>/
// and are selected at compile time by createRenderer().
//
//   Ownership  - A renderer is always owned through RendererPtr, a unique_ptr
//                whose deleter releases the backend back to the allocator that
//                created it (ADR-005): no bare delete, no leaked block, and the
//                exact allocation size is what gets deallocated. The backend
//                also owns its frame-scoped buffers (triangle scratch) through
//                the same allocator, released in its destructor.
//   Backends   - createRenderer() picks the backend at compile time. Today the
//                software backend (F4.2, ADR-004) is wired: it rasterizes into
//                CPU BGRA32 render targets, runs headless, and is therefore the
//                CI-hermetic path (ADR-030). A no-op NullBackend for pure
//                overhead measurement can be added the same way when needed;
//                Vulkan lands in F4.5 behind this same interface (ADR-009).
//   Color space- Work happens in linear space; the backend converts to sRGB at
//                present time, exactly once per written pixel (F4.7, ADR-037).
//   Determinism- The renderer holds no hidden mutable state outside its
//                instance (rule 11): the same clear/draw/present sequence over
//                the same inputs produces the same framebuffer, and the
//                multi-threaded tile path (F4.4) yields the exact same result
//                as the single-threaded path. The software backend allocates
//                nothing in clear/draw/present once its buffers are reserved
//                (rules 03/08): hot-path capacity exhaustion is reported as
//                ALLOCATION_FAILED, never silently grown mid-frame.
#pragma once

#include "infinity/core/allocator.h"
#include "infinity/core/error.h"
#include "infinity/renderer/color.h"
#include "infinity/renderer/draw_list.h"
#include "infinity/renderer/error.h"
#include "infinity/renderer/render_target.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace infinity::renderer {

// Configuration for createRenderer(). allocator is the backend's allocator
// (ADR-005) and must outlive the renderer. tileSize is the edge of the square
// tiles the software backend rasterizes (F4.4); 0 clamps to 1. cullBackfaces
// toggles screen-space backface culling (F4.3). threaded toggles the tile
// thread-pool path (F4.4). autoClear makes present() clear the target to the
// color recorded by clear() before rasterizing; when disabled the caller owns
// the target's initial content and present() draws on top of it.
struct RendererConfig {
    core::Allocator* allocator = nullptr;
    std::uint32_t tileSize = 32;
    bool cullBackfaces = true;
    bool threaded = true;
    bool autoClear = true;
};

// Pure renderer interface. Backends implement the exact same contract so
// swapping the software backend for Vulkan (and back) never touches callers
// (rule 01, ADR-009). All operations are recoverable-failure only: errors
// come back through ExpectedVoid, never through exceptions (rule 04).
class Renderer {
public:
    virtual ~Renderer() = default;

    // Records the clear color used by the next present() when autoClear is
    // enabled. Pure bookkeeping: no target is touched until present().
    [[nodiscard]] virtual ExpectedVoid clear(const Color& color) noexcept = 0;

    // Appends an implicit triangle list (draw_list.h) to the current frame.
    // Rejects a vertex count not divisible by three with INVALID_ARGUMENT and
    // reports ALLOCATION_FAILED when the reserved scratch cannot grow.
    [[nodiscard]] virtual ExpectedVoid draw(const DrawList& list) noexcept = 0;

    // Renders the accumulated frame into target: clears it to the recorded
    // clear color (when autoClear), rasterizes every drawn triangle (tiled and
    // multi-threaded when configured) in linear space converting to sRGB at
    // the write, and consumes the frame so the next clear/draw starts fresh.
    [[nodiscard]] virtual ExpectedVoid present(RenderTarget& target) noexcept = 0;

    // Stable backend identifier for diagnostics ("software").
    [[nodiscard]] virtual std::string_view backendName() const noexcept = 0;
};

// Releases a Renderer back to its owning allocator: explicit destructor call
// followed by deallocate with the exact size used at creation (ADR-005,
// allocator.h). Defined inline because unique_ptr must call it from every TU
// that destroys a RendererPtr.
struct RendererDeleter {
    core::Allocator* allocator = nullptr;
    std::size_t size = 0;

    void operator()(Renderer* renderer) const noexcept {
        if (renderer == nullptr) {
            return;
        }
        renderer->~Renderer();
        allocator->deallocate(renderer, size);
    }
};

using RendererPtr = std::unique_ptr<Renderer, RendererDeleter>;

// Creates a renderer through config.allocator with the given config (rule 04):
// the caller owns the returned renderer and it is released back to the same
// allocator when the RendererPtr goes out of scope. Fails with INVALID_ARGUMENT
// when allocator is null and ALLOCATION_FAILED when the allocator cannot
// satisfy the request. The backend is chosen at compile time.
[[nodiscard]] Expected<RendererPtr> createRenderer(const RendererConfig& config) noexcept;

} // namespace infinity::renderer
