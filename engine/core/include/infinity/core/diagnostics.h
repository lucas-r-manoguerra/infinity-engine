// infinity/core/diagnostics.h
//
// Zero-allocation, thread-safe engine-wide diagnostics counters (F2.6,
// rule 08). Every subsystem reports into one catalog of FIXED, compile-time
// counters: rendering stats (draws, pixels), ECS lifecycle, allocation
// traffic, IO. A hot path may bump a counter every frame without allocating
// (rule 03/08: 0 allocations after init).
//
//   Catalog     - CounterId is the compile-time catalog. Values are STABLE:
//                 counters serialize into telemetry, so existing ids never
//                 change meaning and new counters are APPENDED ONLY (never
//                 renumbered, never removed).
//   Wrap-around - Counters are unsigned and saturate nothing. A wrapped
//                 counter has counted 2^64 of something, which is a
//                 diagnostics bug (the catalog is wrong for the workload),
//                 not a correctness bug in the engine: counters only report
//                 the raw count, they never fail or clamp.
//   Zero alloc  - Counters live in a std::array of atomics stored inside the
//                 Diagnostics object (inline in the instance). Construction,
//                 increment, set, read and reset allocate nothing; the
//                 facility is usable in the frame loop (rule 08).
//   Layout      - One std::atomic<uint64_t> per counter, adjacent, no
//                 padding: the array is ~COUNTER_COUNT*8 bytes and
//                 cache-line-friendly for reads. Counters sharing a cache
//                 line and written from different threads share that line
//                 (false sharing); accepted by design - pad or split only
//                 where the profiler shows it matters (rule 08: measure
//                 before optimizing).
//   Thread-safe - Every operation is one relaxed atomic op: concurrent
//                 increments of a counter are exact (no lost updates).
//                 Relaxed ordering is deliberate - counters are diagnostic
//                 facts, not synchronization primitives; ordering guarantees
//                 belong to the data they describe (rule 11).
//   instance()  - Sanctioned exception to rule 11's "no mutable global
//                 state", mirroring FaultInjector::instance(): diagnostics
//                 are inherently global - systems across modules must report
//                 into ONE object for totals to mean anything, and threading
//                 a Diagnostics through every hot path would pollute APIs.
//                 The singleton is one explicit object; tests that need
//                 isolation construct a local Diagnostics and never touch
//                 instance(). Prefer constructor injection; instance() is
//                 for production wiring that cannot carry one by reference.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace infinity::core {

// The FIXED, compile-time catalog of engine counters (F2.6). Each enumerator
// is one counter; COUNTER_COUNT (below) is the catalog size. CounterId values
// are stable: they serialize into telemetry, so existing ids never change
// meaning and new counters are appended only (see the header brief).
//
// NOLINTNEXTLINE(performance-enum-size) -- deliberate: F2.6 spec pins the base to uint16_t.
enum class CounterId : uint16_t {
    FRAME_COUNT = 0, ///< total frames presented by the runtime (rule 08: hot path, every frame)
    FIXED_UPDATES,   ///< total fixed-timestep updates run (ADR-006)
    ECS_ENTITIES_CREATED,   ///< entities created through the ECS
    ECS_ENTITIES_DESTROYED, ///< entities destroyed through the ECS
    RENDER_DRAWS,           ///< draw calls issued by the renderer
    RENDER_PIXELS,          ///< gauge - pixels written in the current frame; set, never incremented
    ALLOC_BYTES,           ///< bytes allocated through core allocators (arena/pool backing traffic)
    ALLOC_FAILURES,        ///< allocation failures reported as recoverable errors (rule 04)
    IO_BYTES_READ,         ///< bytes read from external data
    IO_BYTES_WRITTEN,      ///< bytes written to external data
    IO_ERRORS,             ///< IO operations that failed (rule 04)
    ALLOC_BUDGET_EXCEEDED, ///< gauge - budget crossings counted by the future runtime/logger
                           ///< consumer. BudgetAllocator does NOT bump this itself: the decorator
                           ///< stays decoupled and the wiring lives where the alert callback is
                           ///< consumed (F2.12, ADR-034).
    FRAME_BUDGET_EXCEEDED, ///< gauge - frame budget crossings counted by the future runtime/logger
                           ///< consumer. TimeBudgets does NOT bump this itself: the tracker stays
                           ///< decoupled and the wiring lives where the alert callback is
                           ///< consumed (F2.14, ADR-055).
    COUNT,                 ///< sentinel - the catalog size, not a counter
};

// Number of live counters (the catalog size), derived from the COUNT sentinel
// so adding a counter grows the counter array automatically. Stable ids and
// append-only growth mean this value never decreases.
inline constexpr size_t COUNTER_COUNT = std::to_underlying(CounterId::COUNT);

// Zero-allocation, thread-safe diagnostics counters (F2.6, rule 08). One
// atomic per counter, addressed by CounterId; see the header brief for the
// catalog, wrap-around, layout and rule-11 tradeoffs.
class Diagnostics {
public:
    // A fresh instance has every counter at zero (the atomic array is
    // value-initialized).
    Diagnostics() noexcept = default;
    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;
    Diagnostics(Diagnostics&&) = delete;
    Diagnostics& operator=(Diagnostics&&) = delete;

    // Adds amount (default 1) to counter id. Atomic, allocation-free.
    void increment(CounterId id, uint64_t amount = 1) noexcept;

    // Stores value into counter id (for gauges like RENDER_PIXELS). Atomic; a
    // plain store, it never composes with concurrent increments.
    void set(CounterId id, uint64_t value) noexcept;

    // Current value of counter id. Atomic load; the snapshot is a single
    // moment in time, not a consistent view across counters.
    [[nodiscard]] uint64_t value(CounterId id) const noexcept;

    // Zeroes every counter. Atomic per counter, not a single atomic snapshot:
    // a concurrent increment may land after its counter was zeroed.
    void reset() noexcept;

    // Engine-wide diagnostics (see the header brief for the rule-11
    // tradeoff). Tests that need isolation construct a local Diagnostics.
    [[nodiscard]] static Diagnostics& instance() noexcept;

private:
    std::array<std::atomic<uint64_t>, COUNTER_COUNT> m_counters{};
};

} // namespace infinity::core
