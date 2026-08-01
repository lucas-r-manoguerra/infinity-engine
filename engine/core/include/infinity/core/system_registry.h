// infinity/core/system_registry.h
// ADR-014: declarative system registry (F2.11, rules 04/11). Systems register
// with a name, declared dependencies, init and shutdown callbacks; the registry
// resolves the init order from dependencies (Kahn topological sort, ties broken
// by registration order) and runs shutdown in exact reverse. Deterministic: the
// same registration sequence always yields the same order (rule 11). Adding or
// removing systems never breaks startup; a dependency cycle is a recoverable
// error (rule 04), never a hang or a crash.
#pragma once

#include "infinity/core/error.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace infinity::core {

class SystemRegistry {
public:
    // Init runs once during initialize() in resolved order and may fail:
    // the first failure stops initialization (ADR-016). Shutdown runs once in
    // reverse order and is best-effort (noexcept).
    using InitFn = ExpectedVoid (*)() noexcept;
    using ShutdownFn = void (*)() noexcept;

    struct System {
        std::string_view name;                          // non-empty, unique
        std::span<const std::string_view> dependencies; // names of systems in the same registry
        InitFn init;
        ShutdownFn shutdown;
    };

    SystemRegistry() noexcept;
    SystemRegistry(const SystemRegistry&) = delete;
    SystemRegistry& operator=(const SystemRegistry&) = delete;
    SystemRegistry(SystemRegistry&&) noexcept = delete;
    SystemRegistry& operator=(SystemRegistry&&) noexcept = delete;

    // Shuts down first if still initialized (RAII, rule 03).
    ~SystemRegistry();

    // Registers a system. The registry does NOT copy name or dependency
    // strings: they must outlive the registry (string literals are the norm).
    // Dependencies are NOT validated here: the whole graph is validated by
    // initialize() (UNKNOWN_DEPENDENCY) and resolved there. Errors:
    // DUPLICATE_SYSTEM (name already registered), ALREADY_INITIALIZED (registry
    // is already initialized), INVALID_ARGUMENT (empty name, null init, or null
    // shutdown). No-op on error.
    [[nodiscard]] ExpectedVoid registerSystem(System system) noexcept;

    // Topologically sorts and runs init() in resolved order. Validates the
    // whole graph before running any init (an unknown dependency or a cycle
    // runs nothing). Stops at the first failing init and returns its exact
    // error; lastFailedSystem() names the system. ALREADY_INITIALIZED when
    // called without an intervening shutdown(). After a failed init the
    // registry is NOT initialized; the systems that already ran are shut down
    // by shutdown().
    [[nodiscard]] ExpectedVoid initialize() noexcept;

    // Runs shutdown() in exact reverse of the init order that actually ran.
    // Idempotent (second call no-op). Safe before initialize() (no-op).
    // Clears the resolved order (order() is empty again).
    void shutdown() noexcept;

    // Resolved init order (names). Empty before initialize() and after
    // shutdown().
    [[nodiscard]] std::span<const std::string_view> order() const noexcept;

    // True between a successful initialize() and the next shutdown().
    [[nodiscard]] bool isInitialized() const noexcept;

    // Name of the system whose init most recently failed; empty if none.
    // Valid while the registry lives and no further systems are registered.
    [[nodiscard]] std::string_view lastFailedSystem() const noexcept;

    // Number of registered systems.
    [[nodiscard]] size_t systemCount() const noexcept;

private:
    std::vector<System> m_systems;           // registration order = tie-break
    std::vector<std::string_view> m_order;   // resolved init order (names)
    std::vector<std::string_view> m_started; // init order that actually ran
    std::string_view m_lastFailedSystem;     // most recent init failure
    bool m_initialized{false};
};

} // namespace infinity::core
