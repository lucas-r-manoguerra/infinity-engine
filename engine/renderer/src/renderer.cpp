// infinity/renderer/src/renderer.cpp
#include "infinity/renderer/renderer.h"

#include "software/software_backend.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <thread>

// Backend selection (rule 01): exactly one backend is wired at compile time by
// the module's CMakeLists.txt. Today that is the software backend
// (INFINITY_RENDERER_SOFTWARE, ADR-004/030); Vulkan lands in F4.5 and adds its
// own marker. Building without any backend is a configuration error, not a
// runtime case.
#if !defined(INFINITY_RENDERER_SOFTWARE) || !INFINITY_RENDERER_SOFTWARE
#error "no renderer backend enabled: define INFINITY_RENDERER_SOFTWARE=1"
#endif

namespace infinity::renderer {

Expected<RendererPtr> createRenderer(const RendererConfig& config) noexcept {
    if (config.allocator == nullptr) {
        return std::unexpected(RenderError::INVALID_ARGUMENT);
    }
    core::Allocator& allocator = *config.allocator;

    // Backend object block (ADR-005): one allocation, released by
    // RendererDeleter with this exact size.
    void* block = allocator.allocate(sizeof(SoftwareBackend), alignof(SoftwareBackend));
    if (block == nullptr) {
        return std::unexpected(RenderError::ALLOCATION_FAILED);
    }

    // The threaded tile path needs a fixed worker pool (F4.4). It is created
    // here so a failed allocation surfaces as a clean error before the backend
    // exists; the backend releases it in its destructor.
    core::ThreadPool* pool = nullptr;
    if (config.threaded) {
        pool = allocator.allocateObject<core::ThreadPool>(alignof(core::ThreadPool));
        if (pool == nullptr) {
            allocator.deallocate(block, sizeof(SoftwareBackend));
            return std::unexpected(RenderError::ALLOCATION_FAILED);
        }
        const std::uint32_t workers = std::max(1u, std::thread::hardware_concurrency());
        new (pool) core::ThreadPool(workers, core::DEFAULT_QUEUE_CAPACITY);
    }

    auto* backend = new (block) SoftwareBackend(config, allocator, pool);
    return RendererPtr(backend,
                       RendererDeleter{.allocator = &allocator, .size = sizeof(SoftwareBackend)});
}

} // namespace infinity::renderer
