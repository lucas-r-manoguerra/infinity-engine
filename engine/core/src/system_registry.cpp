// src/system_registry.cpp
#include "infinity/core/system_registry.h"

#include <cstddef>
#include <expected>
#include <string_view>
#include <vector>

namespace infinity::core {

namespace {

// Linear scan for the registered system with the given name. The registry is
// init-time and small, so a linear scan is fine (rule 08's zero-allocation
// rule applies to the frame, not startup). Returns nullptr when absent.
[[nodiscard]] const SystemRegistry::System*
findSystem(const std::vector<SystemRegistry::System>& systems, std::string_view name) noexcept {
    for (const SystemRegistry::System& system : systems) {
        if (system.name == name) {
            return &system;
        }
    }
    return nullptr;
}

} // namespace

SystemRegistry::SystemRegistry() noexcept = default;

SystemRegistry::~SystemRegistry() { shutdown(); }

ExpectedVoid SystemRegistry::registerSystem(System system) noexcept {
    if (m_initialized) {
        return std::unexpected(CoreError::ALREADY_INITIALIZED);
    }
    if (system.name.empty() || system.init == nullptr || system.shutdown == nullptr) {
        return std::unexpected(CoreError::INVALID_ARGUMENT);
    }
    for (const System& existing : m_systems) {
        if (existing.name == system.name) {
            return std::unexpected(CoreError::DUPLICATE_SYSTEM);
        }
    }
    m_systems.push_back(system);
    return {};
}

ExpectedVoid SystemRegistry::initialize() noexcept {
    if (m_initialized) {
        return std::unexpected(CoreError::ALREADY_INITIALIZED);
    }

    const size_t count = m_systems.size();

    // Validate the declared dependency names against the registered set BEFORE
    // sorting: a dependency that never matches keeps its system from ever
    // becoming ready, which the sort would otherwise misread as a cycle.
    for (const System& system : m_systems) {
        for (const std::string_view dependency : system.dependencies) {
            if (findSystem(m_systems, dependency) == nullptr) {
                m_order.clear();
                return std::unexpected(CoreError::UNKNOWN_DEPENDENCY);
            }
        }
    }

    // Kahn's topological sort. indegree[i] counts the declared dependencies of
    // system i that are not yet scheduled; ready systems (indegree 0) are
    // picked in registration order, which is the deterministic tie-break.
    std::vector<size_t> indegree(count, 0);
    std::vector<bool> scheduled(count, false);
    std::vector<size_t> resolved;
    resolved.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        indegree[i] = m_systems[i].dependencies.size();
    }

    size_t processed = 0;
    while (processed < count) {
        size_t next = count; // sentinel: no ready system found
        for (size_t i = 0; i < count; ++i) {
            if (!scheduled[i] && indegree[i] == 0) {
                next = i;
                break;
            }
        }
        if (next == count) {
            // A dependency cycle: every unscheduled system still has an
            // unsatisfied declared dependency. Nothing ran (rule 04).
            m_order.clear();
            return std::unexpected(CoreError::DEPENDENCY_CYCLE);
        }
        scheduled[next] = true;
        resolved.push_back(next);
        ++processed;
        for (size_t j = 0; j < count; ++j) {
            if (scheduled[j]) {
                continue;
            }
            for (const std::string_view dependency : m_systems[j].dependencies) {
                if (dependency == m_systems[next].name) {
                    --indegree[j];
                    break;
                }
            }
        }
    }

    m_order.clear();
    m_order.reserve(count);
    for (const size_t index : resolved) {
        m_order.push_back(m_systems[index].name);
    }

    m_started.clear();
    m_started.reserve(count);
    for (const size_t index : resolved) {
        const ExpectedVoid result = m_systems[index].init();
        if (!result.has_value()) {
            m_lastFailedSystem = m_systems[index].name;
            return std::unexpected(result.error());
        }
        m_started.push_back(m_systems[index].name);
    }

    m_initialized = true;
    return {};
}

void SystemRegistry::shutdown() noexcept {
    if (!m_initialized && m_started.empty()) {
        return;
    }
    for (size_t i = m_started.size(); i > 0; --i) {
        const System* system = findSystem(m_systems, m_started[i - 1]);
        if (system != nullptr) {
            system->shutdown();
        }
    }
    m_started.clear();
    m_order.clear();
    m_initialized = false;
}

std::span<const std::string_view> SystemRegistry::order() const noexcept { return m_order; }

bool SystemRegistry::isInitialized() const noexcept { return m_initialized; }

std::string_view SystemRegistry::lastFailedSystem() const noexcept { return m_lastFailedSystem; }

size_t SystemRegistry::systemCount() const noexcept { return m_systems.size(); }

} // namespace infinity::core
