// infinity/core/testing/fault_injector.h
//
// Deterministic, thread-safe fault injection for tests (F2.4, ADR-016, rule 06).
// Test doubles (backing allocators, IO wrappers, init gates) probe this
// injector on every invocation and fail when the failure SCRIPT says so.
// Same script -> same behavior (rule 11): there is no randomness, only the
// explicit (key, callIndex, CoreError) plan the test schedules.
//
//   Script model  - A failure script is a set of (key, callIndex, CoreError)
//                   triples. Each key has an independent call counter starting
//                   at 0; every probe(key) serves one call and advances the
//                   counter. A failure scheduled at index N fires exactly on
//                   the Nth probe (0-based) of that key and never again.
//                   Scheduling an index in the past is a no-op that never
//                   fires (the caller's responsibility).
//   Keys          - Keys are stable strings copied into the injector, so
//                   callers may pass string literals safely. Keys are
//                   independent: one key's script never affects another.
//                   failNext() targets the next call, counting its own
//                   unfulfilled reservations, so consecutive failNext calls
//                   make consecutive calls fail.
//   Duplicates    - Multiple failures per key and out-of-order callIndex are
//                   allowed; they fire in callIndex order. When several
//                   failures are scheduled at the same index, the first
//                   scheduled one fires and all are consumed together.
//   Thread safety - Every public method takes the same std::mutex. The call
//                   counter advances atomically with the failure lookup, so
//                   each index is served exactly once no matter how many
//                   threads probe concurrently.
//
// TEST-ONLY FACILITY. This header lives under include/infinity/core/testing/
// and is intended for test translation units; no production header references
// it. Fault-aware production code whose test doubles probe the injector (e.g.
// the in-memory filesystem backend, F2.8) may include it from its .cpp only.
// instance() is a deliberate exception to rule 11's "no mutable global state": the type exists
// solely in test binaries, and threading an injector through production signatures (allocator
// ctors, IO opens, init) would pollute APIs that must stay stable. Prefer constructor injection
// (see the FaultInjectingAllocator double in fault_injector_test.cpp) and fall
// back to instance() only where a signature cannot carry the injector.
#pragma once

#include "infinity/core/error.h"

#include <cstdint>
#include <expected>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

namespace infinity::core::testing {

// Deterministic failure-script control for test doubles (ADR-016).
class FaultInjector {
public:
    FaultInjector() = default;
    FaultInjector(const FaultInjector&) = delete;
    FaultInjector& operator=(const FaultInjector&) = delete;
    FaultInjector(FaultInjector&&) = delete;
    FaultInjector& operator=(FaultInjector&&) = delete;

    // Schedules that key's callIndex-th invocation (0-based) to fail with
    // error. Multiple entries per key and out-of-order indices are allowed;
    // see the class brief for the duplicate-index rule.
    void enqueue(const char* key, uint64_t callIndex, CoreError error);

    // Convenience: schedules the NEXT invocation of key to fail, counting
    // prior unfulfilled failNext reservations, so two consecutive failNext
    // calls make the first two calls fail.
    void failNext(const char* key, CoreError error);

    // Serves one call for key: advances the counter and returns the scheduled
    // error when the served index had one, or success otherwise. Fault-aware
    // code calls this on every invocation and fails when it gets an error.
    [[nodiscard]] ExpectedVoid probe(const char* key);

    // Clears every key (counters and scheduled failures).
    void reset();

    // Clears one key (its counter and scheduled failures). No-op when the key
    // has no state.
    void clear(const char* key);

    // Process-wide injector for TEST-ONLY wiring that cannot carry one by
    // reference. See the header brief for the rule-11 tradeoff.
    [[nodiscard]] static FaultInjector& instance();

private:
    // Per-key script state. Struct members are camelCase: this is a data
    // aggregate, not a class with private members (rule 02).
    struct KeyState {
        uint64_t nextIndex{0};                       // index of the next probe to serve
        uint64_t pendingFailNext{0};                 // failNext reservations not yet fulfilled
        std::multimap<uint64_t, CoreError> failures; // scheduled failures by call index
    };

    // Returns the state for key, creating it (counter 0, no failures) on first
    // use. References stay valid across map rehashes.
    [[nodiscard]] KeyState& stateFor(const char* key);

    static void schedule(KeyState& state, uint64_t callIndex, CoreError error);

    std::unordered_map<std::string, KeyState> m_states;
    std::mutex m_mutex;
};

// --- inline implementation (header-only test support) --------------------

inline void FaultInjector::enqueue(const char* key, uint64_t callIndex, CoreError error) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    schedule(stateFor(key), callIndex, error);
}

inline void FaultInjector::failNext(const char* key, CoreError error) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    KeyState& state = stateFor(key);
    schedule(state, state.nextIndex + state.pendingFailNext, error);
    ++state.pendingFailNext;
}

inline ExpectedVoid FaultInjector::probe(const char* key) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    KeyState& state = stateFor(key);
    const uint64_t index = state.nextIndex;
    ++state.nextIndex;
    const auto [first, last] = state.failures.equal_range(index);
    if (first == last) {
        return {};
    }
    const CoreError error = first->second;
    state.failures.erase(first, last);
    if (state.pendingFailNext > 0) {
        --state.pendingFailNext;
    }
    return std::unexpected(error);
}

inline void FaultInjector::reset() {
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_states.clear();
}

inline void FaultInjector::clear(const char* key) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_states.erase(std::string(key));
}

inline FaultInjector& FaultInjector::instance() {
    static FaultInjector injector;
    return injector;
}

inline FaultInjector::KeyState& FaultInjector::stateFor(const char* key) {
    return m_states.try_emplace(key).first->second;
}

inline void FaultInjector::schedule(KeyState& state, uint64_t callIndex, CoreError error) {
    state.failures.emplace(callIndex, error);
}

} // namespace infinity::core::testing
