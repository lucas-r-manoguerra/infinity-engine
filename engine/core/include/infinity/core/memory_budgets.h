// infinity/core/memory_budgets.h
//
// Engine-side memory budget tracker (F2.12, ADR-034, rules 04/08/11). Each
// subsystem declares a byte budget; the tracker registers the subsystem's
// BudgetAllocator and exposes per-subsystem and aggregate queries for the
// future dev console / logger. Registration is init-time wiring, never a hot
// path (rule 08): per-subsystem queries are O(1) array lookups and the totals
// are O(BUDGET_COUNT) sums over pre-registered pointers - zero allocation.
//
//   Catalog    - BudgetId is the compile-time catalog of subsystems (the
//                rule-01 module contract). Values are STABLE: ids serialize
//                into telemetry, so existing ids never change meaning and new
//                subsystems are APPENDED ONLY (never renumbered, never
//                removed, never renamed). Add new ids before COUNT.
//   Registry   - registerBudget() wires a BudgetAllocator to a subsystem id.
//                Out-of-range ids and duplicate registrations are recoverable
//                errors (rule 04). The tracker never owns allocators: each
//                registered allocator must outlive the registration and the
//                tracker instance.
//   Queries    - budget()/usedBytes() are O(1); totalUsedBytes() and
//                totalBudgetBytes() aggregate over registered allocators and
//                skip unregistered entries. An unlimited allocator
//                (budgetBytes == 0) contributes 0 to totalBudgetBytes: an
//                unlimited budget is not a budget (ADR-034).
//   instance() - Sanctioned exception to rule 11's "no mutable global state",
//                mirroring Diagnostics::instance(): production wiring needs
//                one place where every subsystem registers. The singleton is
//                one explicit object; tests that need isolation construct a
//                local MemoryBudgets and never touch instance().
#pragma once

#include "infinity/core/budget_allocator.h"
#include "infinity/core/error.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace infinity::core {

// The FIXED, compile-time catalog of subsystem budgets (F2.12, ADR-034). Each
// enumerator is one subsystem from the module contract (rule 01); COUNT is the
// catalog size, not a subsystem. BudgetId values are stable: they serialize
// into telemetry, so existing ids never change meaning and new subsystems are
// appended only (see the header brief). The base is pinned to uint16_t like
// CounterId (diagnostics.h).
// NOLINTNEXTLINE(performance-enum-size) -- deliberate, see above.
enum class BudgetId : uint16_t {
    CORE = 0,  ///< core module (allocators, fs, time, ...)
    RUNTIME,   ///< runtime module (app lifecycle, main loop)
    RENDERER,  ///< renderer module (software rasterizer)
    ECS,       ///< ecs module (archetypes, queries)
    AI,        ///< ai module (reads ContextSnapshot)
    BLUEPRINT, ///< blueprint module (graph -> executable)
    ASSETS,    ///< assets module (F9: streaming, procedural generation)
    NETWORK,   ///< network module (F15: server authority)
    COUNT,     ///< sentinel - the catalog size, not a subsystem
};

// Number of subsystems (the catalog size), derived from the COUNT sentinel so
// adding a subsystem grows the storage automatically. Stable ids and
// append-only growth mean this value never decreases.
inline constexpr size_t BUDGET_COUNT = std::to_underlying(BudgetId::COUNT);

// Engine-side tracker of per-subsystem memory budgets (F2.12, ADR-034). One
// non-owning pointer per subsystem; see the header brief for the catalog,
// registry, queries and rule-11 tradeoff.
class MemoryBudgets {
public:
    // A fresh instance has no registered budgets (the pointer array is
    // value-initialized).
    MemoryBudgets() noexcept = default;
    MemoryBudgets(const MemoryBudgets&) = delete;
    MemoryBudgets& operator=(const MemoryBudgets&) = delete;
    MemoryBudgets(MemoryBudgets&&) noexcept = delete;
    MemoryBudgets& operator=(MemoryBudgets&&) noexcept = delete;

    // Registers allocator as the budget of subsystem id. id must name a
    // catalog id (COUNT and beyond are invalid). Errors: INVALID_ARGUMENT (id
    // out of range), DUPLICATE_BUDGET (id already registered). No-op on error:
    // a failed registration changes nothing. Registration is init-time wiring,
    // never a hot path.
    [[nodiscard]] ExpectedVoid registerBudget(BudgetId id, BudgetAllocator& allocator) noexcept;

    // Registered allocator of subsystem id, or nullptr when not registered.
    // O(1), no allocation, no failure mode. id must name a catalog id.
    [[nodiscard]] const BudgetAllocator* budget(BudgetId id) const noexcept;

    // Live bytes of subsystem id, or 0 when not registered. O(1), no
    // allocation. id must name a catalog id.
    [[nodiscard]] size_t usedBytes(BudgetId id) const noexcept;

    // Sum of live bytes over every registered allocator. O(BUDGET_COUNT), no
    // allocation.
    [[nodiscard]] size_t totalUsedBytes() const noexcept;

    // Sum of declared budgets over every registered allocator. Unregistered
    // entries and unlimited allocators (budgetBytes == 0) contribute 0.
    // O(BUDGET_COUNT), no allocation.
    [[nodiscard]] size_t totalBudgetBytes() const noexcept;

    // Engine-wide tracker (see the header brief for the rule-11 tradeoff).
    // Tests that need isolation construct a local MemoryBudgets.
    [[nodiscard]] static MemoryBudgets& instance() noexcept;

private:
    std::array<BudgetAllocator*, BUDGET_COUNT> m_allocators{};
};

} // namespace infinity::core
