// tests/core/system_registry_order_test.cpp
//
// SystemRegistry order and lifecycle tests (F2.11, ADR-014, rules 04 and 11):
// systems register declaratively with name, dependencies and init/shutdown
// callbacks; the registry resolves the init order from the declared
// dependencies (Kahn topological sort, ties by registration order) and runs
// shutdown in exact reverse. Determinism is the contract: the same registration
// sequence always yields the same order (rule 11), adding or removing a system
// never breaks startup.
//
// Error paths (cycles, unknown dependencies, init failures) live in
// system_registry_error_test.cpp. Init/shutdown callbacks are function
// pointers (no captures, rule 08), so call order is recorded in file-scope
// test instrumentation - not engine state (rule 11), mirroring loop_test.cpp.
#include "infinity/core/system_registry.h"

#include <array>
#include <cstddef>
#include <expected>
#include <initializer_list>
#include <span>
#include <string_view>

#include <doctest/doctest.h>

namespace {

using infinity::core::ExpectedVoid;
using infinity::core::SystemRegistry;

constexpr size_t MAX_SYSTEMS = 6;
constexpr std::array<std::string_view, MAX_SYSTEMS> SYSTEM_NAMES{"A", "B", "C", "D", "E", "F"};

// Test instrumentation: the order in which init/shutdown callbacks ran.
// File-scope because the callbacks are function pointers (loop_test.cpp
// pattern); harness state, not engine state (rule 11).
std::array<std::string_view, MAX_SYSTEMS> g_initOrder{};
size_t g_initCount = 0;
std::array<std::string_view, MAX_SYSTEMS> g_shutdownOrder{};
size_t g_shutdownCount = 0;

void resetRecorder() {
    g_initCount = 0;
    g_shutdownCount = 0;
}

// Distinct function address per system (template id); each records its name
// so tests can assert the exact run order.
template <int Id> ExpectedVoid initSystem() noexcept {
    g_initOrder[g_initCount++] = SYSTEM_NAMES[static_cast<size_t>(Id)];
    return {};
}

template <int Id> void shutdownSystem() noexcept {
    g_shutdownOrder[g_shutdownCount++] = SYSTEM_NAMES[static_cast<size_t>(Id)];
}

SystemRegistry::System makeSystem(std::string_view name,
                                  std::span<const std::string_view> dependencies,
                                  SystemRegistry::InitFn init,
                                  SystemRegistry::ShutdownFn shutdown) {
    return SystemRegistry::System{
        .name = name, .dependencies = dependencies, .init = init, .shutdown = shutdown};
}

bool initOrderRan(std::initializer_list<std::string_view> expected) noexcept {
    if (g_initCount != expected.size()) {
        return false;
    }
    size_t i = 0;
    for (const std::string_view name : expected) {
        if (g_initOrder[i] != name) {
            return false;
        }
        ++i;
    }
    return true;
}

bool shutdownOrderRan(std::initializer_list<std::string_view> expected) noexcept {
    if (g_shutdownCount != expected.size()) {
        return false;
    }
    size_t i = 0;
    for (const std::string_view name : expected) {
        if (g_shutdownOrder[i] != name) {
            return false;
        }
        ++i;
    }
    return true;
}

bool orderEquals(const SystemRegistry& registry,
                 std::initializer_list<std::string_view> expected) noexcept {
    if (registry.order().size() != expected.size()) {
        return false;
    }
    size_t i = 0;
    for (const std::string_view name : expected) {
        if (registry.order()[i] != name) {
            return false;
        }
        ++i;
    }
    return true;
}

} // namespace

TEST_CASE("init order follows dependencies when registered in reverse") {
    resetRecorder();
    SystemRegistry registry;

    const std::array<std::string_view, 1> bDeps{"A"};
    const std::array<std::string_view, 1> cDeps{"B"};

    CHECK(registry.registerSystem(makeSystem("C", cDeps, &initSystem<2>, &shutdownSystem<2>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("B", bDeps, &initSystem<1>, &shutdownSystem<1>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("A", {}, &initSystem<0>, &shutdownSystem<0>))
              .has_value());

    CHECK(registry.initialize().has_value());

    CHECK(orderEquals(registry, {"A", "B", "C"}));
    CHECK(initOrderRan({"A", "B", "C"}));
    CHECK(registry.systemCount() == 3);
}

TEST_CASE("independent systems init in registration order (deterministic ties)") {
    resetRecorder();
    SystemRegistry registry;

    CHECK(registry.registerSystem(makeSystem("A", {}, &initSystem<0>, &shutdownSystem<0>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("B", {}, &initSystem<1>, &shutdownSystem<1>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("C", {}, &initSystem<2>, &shutdownSystem<2>))
              .has_value());

    CHECK(registry.initialize().has_value());

    CHECK(orderEquals(registry, {"A", "B", "C"}));
    CHECK(initOrderRan({"A", "B", "C"}));
}

TEST_CASE("the same registration sequence always yields the same order (rule 11)") {
    resetRecorder();
    SystemRegistry registry;

    const std::array<std::string_view, 1> bDeps{"A"};
    const std::array<std::string_view, 1> cDeps{"B"};
    const std::array<std::string_view, 1> dDeps{"A"};

    CHECK(registry.registerSystem(makeSystem("D", dDeps, &initSystem<3>, &shutdownSystem<3>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("C", cDeps, &initSystem<2>, &shutdownSystem<2>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("A", {}, &initSystem<0>, &shutdownSystem<0>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("B", bDeps, &initSystem<1>, &shutdownSystem<1>))
              .has_value());

    CHECK(registry.initialize().has_value());
    CHECK(orderEquals(registry, {"A", "D", "B", "C"}));
    CHECK(initOrderRan({"A", "D", "B", "C"}));

    registry.shutdown();
    CHECK(orderEquals(registry, {}));

    resetRecorder();
    CHECK(registry.initialize().has_value());
    CHECK(orderEquals(registry, {"A", "D", "B", "C"}));
    CHECK(initOrderRan({"A", "D", "B", "C"}));
    CHECK(registry.isInitialized());
}

TEST_CASE("shutdown runs in exact reverse of the init order") {
    resetRecorder();
    SystemRegistry registry;

    const std::array<std::string_view, 1> bDeps{"A"};
    const std::array<std::string_view, 1> cDeps{"B"};

    CHECK(registry.registerSystem(makeSystem("A", {}, &initSystem<0>, &shutdownSystem<0>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("B", bDeps, &initSystem<1>, &shutdownSystem<1>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("C", cDeps, &initSystem<2>, &shutdownSystem<2>))
              .has_value());

    CHECK(registry.initialize().has_value());
    CHECK(initOrderRan({"A", "B", "C"}));

    registry.shutdown();

    CHECK(shutdownOrderRan({"C", "B", "A"}));
    CHECK(orderEquals(registry, {}));
    CHECK_FALSE(registry.isInitialized());
}

TEST_CASE("shutdown is safe before init and idempotent") {
    resetRecorder();
    SystemRegistry registry;

    registry.shutdown();
    CHECK(orderEquals(registry, {}));
    CHECK_FALSE(registry.isInitialized());

    CHECK(registry.registerSystem(makeSystem("A", {}, &initSystem<0>, &shutdownSystem<0>))
              .has_value());
    CHECK(registry.initialize().has_value());

    registry.shutdown();
    registry.shutdown();

    CHECK(shutdownOrderRan({"A"}));
    CHECK(orderEquals(registry, {}));
    CHECK_FALSE(registry.isInitialized());
}

TEST_CASE("order reports the resolved names between init and shutdown") {
    resetRecorder();
    SystemRegistry registry;

    CHECK(orderEquals(registry, {}));
    CHECK(registry.systemCount() == 0);

    const std::array<std::string_view, 1> bDeps{"A"};

    CHECK(registry.registerSystem(makeSystem("A", {}, &initSystem<0>, &shutdownSystem<0>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("B", bDeps, &initSystem<1>, &shutdownSystem<1>))
              .has_value());

    CHECK(registry.initialize().has_value());
    CHECK(orderEquals(registry, {"A", "B"}));

    registry.shutdown();
    CHECK(orderEquals(registry, {}));
}

TEST_CASE("re-initialize after shutdown works and yields the same order") {
    resetRecorder();
    SystemRegistry registry;

    const std::array<std::string_view, 1> bDeps{"A"};

    CHECK(registry.registerSystem(makeSystem("A", {}, &initSystem<0>, &shutdownSystem<0>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("B", bDeps, &initSystem<1>, &shutdownSystem<1>))
              .has_value());

    CHECK(registry.initialize().has_value());
    CHECK(orderEquals(registry, {"A", "B"}));

    registry.shutdown();
    CHECK(orderEquals(registry, {}));

    resetRecorder();
    CHECK(registry.initialize().has_value());
    CHECK(orderEquals(registry, {"A", "B"}));
    CHECK(initOrderRan({"A", "B"}));
    CHECK(registry.isInitialized());
}
