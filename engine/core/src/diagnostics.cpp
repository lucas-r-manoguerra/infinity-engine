// src/diagnostics.cpp
#include "infinity/core/diagnostics.h"

#include <cassert>

namespace infinity::core {

namespace {

// True when id names a live counter (rule 04): an id outside the catalog is a
// caller bug - a programming invariant, asserted in debug like every core
// invariant. Release keeps the raw array access; counters are hot-path and
// bounds-checking is not (rule 08).
[[maybe_unused]] [[nodiscard]] bool isValidCounter(CounterId id) noexcept {
    return std::to_underlying(id) < COUNTER_COUNT;
}

} // namespace

void Diagnostics::increment(CounterId id, uint64_t amount) noexcept {
    assert(isValidCounter(id));
    m_counters[static_cast<size_t>(id)].fetch_add(amount, std::memory_order_relaxed);
}

void Diagnostics::set(CounterId id, uint64_t value) noexcept {
    assert(isValidCounter(id));
    m_counters[static_cast<size_t>(id)].store(value, std::memory_order_relaxed);
}

uint64_t Diagnostics::value(CounterId id) const noexcept {
    assert(isValidCounter(id));
    return m_counters[static_cast<size_t>(id)].load(std::memory_order_relaxed);
}

void Diagnostics::reset() noexcept {
    for (auto& counter : m_counters) {
        counter.store(0, std::memory_order_relaxed);
    }
}

} // namespace infinity::core
