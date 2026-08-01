// tests/core/system_registry_error_test.cpp
//
// SystemRegistry error-path tests (F2.11, ADR-014, rules 04 and 11): every
// declared failure mode is a recoverable error (std::expected), never a hang
// or a panic - dependency cycles, unknown dependencies, duplicate or invalid
// registrations, and init failures that stop the sequence and run shutdown
// only for the systems that started. Dependencies resolve at initialize()
// time: registerSystem accepts any dependency name and initialize() validates
// the whole graph (decision A: deferred resolution).
//
// Order and lifecycle behavior (deterministic topological sort, reverse
// shutdown, re-init) live in system_registry_order_test.cpp. Init/shutdown
// callbacks are function pointers (no captures, rule 08), so call order is
// recorded in file-scope test instrumentation - not engine state (rule 11),
// mirroring loop_test.cpp.
#include "infinity/core/system_registry.h"

#include <array>
#include <cstddef>
#include <expected>
#include <initializer_list>
#include <ostream>
#include <span>
#include <string_view>

#include <doctest/doctest.h>

namespace {

using infinity::core::CoreError;
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

template <int Id> ExpectedVoid failingInitSystem() noexcept {
    g_initOrder[g_initCount++] = SYSTEM_NAMES[static_cast<size_t>(Id)];
    return std::unexpected(CoreError::NOT_FOUND);
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

// Comparisons isolated from CHECK so doctest never stringifies a CoreError
// operand (ADL: infinity::core::toString wins over doctest's - see
// error_test.cpp).
bool isError(const ExpectedVoid& result, CoreError expected) noexcept {
    return !result.has_value() && result.error() == expected;
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

TEST_CASE("a dependency cycle is DEPENDENCY_CYCLE and runs no init") {
    resetRecorder();
    SystemRegistry registry;

    const std::array<std::string_view, 1> aDeps{"B"};
    const std::array<std::string_view, 1> bDeps{"A"};

    CHECK(registry.registerSystem(makeSystem("A", aDeps, &initSystem<0>, &shutdownSystem<0>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("B", bDeps, &initSystem<1>, &shutdownSystem<1>))
              .has_value());

    CHECK(isError(registry.initialize(), CoreError::DEPENDENCY_CYCLE));
    CHECK(g_initCount == 0);
    CHECK_FALSE(registry.isInitialized());
    CHECK(orderEquals(registry, {}));
}

TEST_CASE("a self-dependency is a dependency cycle") {
    resetRecorder();
    SystemRegistry registry;

    const std::array<std::string_view, 1> aDeps{"A"};

    CHECK(registry.registerSystem(makeSystem("A", aDeps, &initSystem<0>, &shutdownSystem<0>))
              .has_value());

    CHECK(isError(registry.initialize(), CoreError::DEPENDENCY_CYCLE));
    CHECK(g_initCount == 0);
    CHECK_FALSE(registry.isInitialized());
}

TEST_CASE("an unknown dependency is UNKNOWN_DEPENDENCY and runs no init") {
    resetRecorder();
    SystemRegistry registry;

    const std::array<std::string_view, 1> ghostDeps{"ghost"};

    CHECK(registry.registerSystem(makeSystem("A", {}, &initSystem<0>, &shutdownSystem<0>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("B", ghostDeps, &initSystem<1>, &shutdownSystem<1>))
              .has_value());

    CHECK(isError(registry.initialize(), CoreError::UNKNOWN_DEPENDENCY));
    CHECK(g_initCount == 0);
    CHECK_FALSE(registry.isInitialized());
    CHECK(orderEquals(registry, {}));
    CHECK(registry.systemCount() == 2);
}

TEST_CASE("a duplicate system name is DUPLICATE_SYSTEM at registration") {
    resetRecorder();
    SystemRegistry registry;

    CHECK(registry.registerSystem(makeSystem("A", {}, &initSystem<0>, &shutdownSystem<0>))
              .has_value());
    CHECK(isError(registry.registerSystem(makeSystem("A", {}, &initSystem<1>, &shutdownSystem<1>)),
                  CoreError::DUPLICATE_SYSTEM));
    CHECK(registry.systemCount() == 1);
}

TEST_CASE("an empty name or null callback is INVALID_ARGUMENT at registration") {
    resetRecorder();
    SystemRegistry registry;

    CHECK(isError(registry.registerSystem(makeSystem("", {}, &initSystem<0>, &shutdownSystem<0>)),
                  CoreError::INVALID_ARGUMENT));
    CHECK(isError(registry.registerSystem(makeSystem("A", {}, nullptr, &shutdownSystem<0>)),
                  CoreError::INVALID_ARGUMENT));
    CHECK(isError(registry.registerSystem(makeSystem("B", {}, &initSystem<0>, nullptr)),
                  CoreError::INVALID_ARGUMENT));
    CHECK(registry.systemCount() == 0);
}

TEST_CASE("init stops at the first failing system and reports its error") {
    resetRecorder();
    SystemRegistry registry;

    const std::array<std::string_view, 1> bDeps{"A"};
    const std::array<std::string_view, 1> cDeps{"B"};

    CHECK(registry.registerSystem(makeSystem("A", {}, &initSystem<0>, &shutdownSystem<0>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("B", bDeps, &failingInitSystem<1>, &shutdownSystem<1>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("C", cDeps, &initSystem<2>, &shutdownSystem<2>))
              .has_value());

    const ExpectedVoid result = registry.initialize();
    CHECK(isError(result, CoreError::NOT_FOUND));
    CHECK(registry.lastFailedSystem() == "B");
    CHECK(initOrderRan({"A", "B"}));
    CHECK_FALSE(registry.isInitialized());
}

TEST_CASE("shutdown after a failed init runs only the started systems, in reverse") {
    resetRecorder();
    SystemRegistry registry;

    const std::array<std::string_view, 1> bDeps{"A"};
    const std::array<std::string_view, 1> cDeps{"B"};

    CHECK(registry.registerSystem(makeSystem("A", {}, &initSystem<0>, &shutdownSystem<0>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("B", bDeps, &failingInitSystem<1>, &shutdownSystem<1>))
              .has_value());
    CHECK(registry.registerSystem(makeSystem("C", cDeps, &initSystem<2>, &shutdownSystem<2>))
              .has_value());

    CHECK(isError(registry.initialize(), CoreError::NOT_FOUND));

    registry.shutdown();

    CHECK(shutdownOrderRan({"A"}));
    CHECK(orderEquals(registry, {}));
    CHECK_FALSE(registry.isInitialized());
}

TEST_CASE("initialize twice without shutdown is ALREADY_INITIALIZED") {
    resetRecorder();
    SystemRegistry registry;

    CHECK(registry.registerSystem(makeSystem("A", {}, &initSystem<0>, &shutdownSystem<0>))
              .has_value());

    CHECK(registry.initialize().has_value());
    CHECK(isError(registry.initialize(), CoreError::ALREADY_INITIALIZED));
    CHECK(registry.isInitialized());
}

TEST_CASE("registering a system after initialize is ALREADY_INITIALIZED") {
    resetRecorder();
    SystemRegistry registry;

    CHECK(registry.registerSystem(makeSystem("A", {}, &initSystem<0>, &shutdownSystem<0>))
              .has_value());
    CHECK(registry.initialize().has_value());

    CHECK(isError(registry.registerSystem(makeSystem("B", {}, &initSystem<1>, &shutdownSystem<1>)),
                  CoreError::ALREADY_INITIALIZED));
    CHECK(registry.systemCount() == 1);
}
