// tests/core/fault_injector_test.cpp
//
// FaultInjector contract and integration tests (F2.4, ADR-016, rules 06/11).
// The unit cases pin the failure-script model: indexed failures, independent
// keys, out-of-order scheduling, failNext, reset/clear and concurrent probing.
// The integration cases wire the injector through a backing-allocator test
// double to exercise the empty-on-failure branches that arena_allocator.h and
// pool_allocator.h document (capacity 0, every allocate returns nullptr).
//
// Reachable error branches (rule 06): arena/pool/allocator declare failure as
// nullptr returns, not std::expected, so the only failure branches reachable
// without touching production code are the documented empty-on-failure
// construction paths below (arena_allocator.h:18-21, pool_allocator.h:23-25).
// error.h's ExpectedVoid alias is exercised by every probe(); the allocator
// headers declare no std::expected-returning API of their own.
//
// ADL note (same as error_test.cpp): infinity::core::toString would win ADL
// over doctest's toString for a CoreError operand, so no CHECK ever compares a
// CoreError directly; failsWith()/lastFailureName() expose the stable names.
#include "infinity/core/arena_allocator.h"
#include "infinity/core/pool_allocator.h"
#include "infinity/core/testing/fault_injector.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <string_view>
#include <thread>

#include <doctest/doctest.h>

namespace {

using infinity::core::CoreError;
using infinity::core::ExpectedVoid;
using infinity::core::testing::FaultInjector;

constexpr std::string_view BACKING_ALLOCATOR_FAILED = "backing_allocator_failed";
constexpr std::string_view NONE = "none";

// True when result is an error carrying expected. Isolates CoreError operands
// from CHECK so doctest never stringifies one (ADL note, see file brief).
[[nodiscard]] bool failsWith(ExpectedVoid result, CoreError expected) noexcept {
    return !result.has_value() &&
           infinity::core::toString(result.error()) == infinity::core::toString(expected);
}

// Allocator test double that consults a FaultInjector before every allocate:
// the fault-aware path ADR-016 exists for. The injector key is fixed per
// backing so a script targets exactly this backing's allocate calls.
class FaultInjectingAllocator final : public infinity::core::Allocator {
public:
    explicit FaultInjectingAllocator(FaultInjector& injector) noexcept : m_injector(injector) {}

    [[nodiscard]] void* allocate(size_t size, size_t alignment) noexcept override {
        if (!infinity::core::isValidAlignment(alignment) || alignment > alignof(std::max_align_t)) {
            return nullptr;
        }
        ++m_allocationCount;
        const ExpectedVoid result = m_injector.probe("backing.allocate");
        if (!result.has_value()) {
            m_lastFailureName = infinity::core::toString(result.error());
            return nullptr;
        }
        return std::malloc(size);
    }

    void deallocate(void* ptr, size_t size) noexcept override {
        (void)size;
        ++m_deallocationCount;
        std::free(ptr);
    }

    [[nodiscard]] bool supportsAlignment(size_t alignment) const noexcept override {
        return infinity::core::isValidAlignment(alignment);
    }

    [[nodiscard]] size_t allocationCount() const noexcept { return m_allocationCount; }

    [[nodiscard]] size_t deallocationCount() const noexcept { return m_deallocationCount; }

    // Stable name of the last failure the script produced, or "none" when the
    // last allocate succeeded. Keeps CoreError out of CHECK (ADL note).
    [[nodiscard]] std::string_view lastFailureName() const noexcept { return m_lastFailureName; }

private:
    FaultInjector& m_injector; // must outlive this double
    std::string_view m_lastFailureName{NONE};
    size_t m_allocationCount{0};
    size_t m_deallocationCount{0};
};

// Probes the "shared" key probeCount times, tallying failures and successes.
// Used by the concurrency smoke test; the totals must match the script.
void probeShared(FaultInjector& injector, uint64_t probeCount, std::atomic<uint64_t>& failures,
                 std::atomic<uint64_t>& successes) {
    for (uint64_t i = 0; i < probeCount; ++i) {
        if (injector.probe("shared").has_value()) {
            ++successes;
        } else {
            ++failures;
        }
    }
}

} // namespace

TEST_CASE("failNext makes exactly the next probe fail") {
    FaultInjector injector;

    injector.failNext("alloc", CoreError::ALLOCATION_FAILED);

    CHECK(failsWith(injector.probe("alloc"), CoreError::ALLOCATION_FAILED));
    CHECK(injector.probe("alloc").has_value());
    CHECK(injector.probe("alloc").has_value());
}

TEST_CASE("enqueued failures fire exactly at their call indices") {
    FaultInjector injector;
    injector.enqueue("io", 2, CoreError::IO_ERROR);
    injector.enqueue("io", 5, CoreError::IO_ERROR);
    injector.enqueue("io", 8, CoreError::IO_ERROR);

    CHECK(injector.probe("io").has_value());
    CHECK(injector.probe("io").has_value());
    CHECK(failsWith(injector.probe("io"), CoreError::IO_ERROR));
    CHECK(injector.probe("io").has_value());
    CHECK(injector.probe("io").has_value());
    CHECK(failsWith(injector.probe("io"), CoreError::IO_ERROR));
    CHECK(injector.probe("io").has_value());
    CHECK(injector.probe("io").has_value());
    CHECK(failsWith(injector.probe("io"), CoreError::IO_ERROR));
    CHECK(injector.probe("io").has_value());
}

TEST_CASE("keys are independent") {
    FaultInjector injector;
    injector.enqueue("alpha", 0, CoreError::TIMEOUT);
    injector.enqueue("beta", 3, CoreError::NOT_FOUND);

    CHECK(failsWith(injector.probe("alpha"), CoreError::TIMEOUT));
    CHECK(injector.probe("alpha").has_value());
    CHECK(injector.probe("alpha").has_value());

    CHECK(injector.probe("beta").has_value());
    CHECK(injector.probe("beta").has_value());
    CHECK(injector.probe("beta").has_value());
    CHECK(failsWith(injector.probe("beta"), CoreError::NOT_FOUND));
}

TEST_CASE("out-of-order enqueue fires failures in call index order") {
    FaultInjector injector;
    injector.enqueue("net", 5, CoreError::IO_PERMISSION_DENIED);
    injector.enqueue("net", 1, CoreError::IO_INVALID_DATA);

    CHECK(injector.probe("net").has_value());
    CHECK(failsWith(injector.probe("net"), CoreError::IO_INVALID_DATA));
    CHECK(injector.probe("net").has_value());
    CHECK(injector.probe("net").has_value());
    CHECK(injector.probe("net").has_value());
    CHECK(failsWith(injector.probe("net"), CoreError::IO_PERMISSION_DENIED));
}

TEST_CASE("two failNext calls make the first two probes fail") {
    FaultInjector injector;
    injector.failNext("sched", CoreError::ALLOCATION_FAILED);
    injector.failNext("sched", CoreError::INVALID_SIZE);

    CHECK(failsWith(injector.probe("sched"), CoreError::ALLOCATION_FAILED));
    CHECK(failsWith(injector.probe("sched"), CoreError::INVALID_SIZE));
    CHECK(injector.probe("sched").has_value());
    CHECK(injector.probe("sched").has_value());
}

TEST_CASE("reset clears every key and clear removes a single key") {
    FaultInjector injector;
    injector.enqueue("a", 0, CoreError::TIMEOUT);
    injector.enqueue("b", 0, CoreError::NOT_FOUND);

    injector.clear("a");
    CHECK(injector.probe("a").has_value());
    CHECK(failsWith(injector.probe("b"), CoreError::NOT_FOUND));

    injector.reset();
    CHECK(injector.probe("b").has_value());
    CHECK(injector.probe("a").has_value());
}

TEST_CASE("concurrent probing consumes the script exactly once") {
    FaultInjector injector;
    constexpr uint64_t FAILURE_INDICES = 1000;
    for (uint64_t index = 0; index < FAILURE_INDICES; ++index) {
        injector.enqueue("shared", index, CoreError::ALLOCATION_FAILED);
    }

    constexpr uint64_t PROBES_PER_THREAD = 10000;
    std::atomic<uint64_t> failures{0};
    std::atomic<uint64_t> successes{0};
    std::thread first{[&] { probeShared(injector, PROBES_PER_THREAD, failures, successes); }};
    std::thread second{[&] { probeShared(injector, PROBES_PER_THREAD, failures, successes); }};
    first.join();
    second.join();

    CHECK(failures.load() == FAILURE_INDICES);
    CHECK(successes.load() == (2 * PROBES_PER_THREAD) - FAILURE_INDICES);
}

TEST_CASE("instance returns a stable process-wide injector") {
    CHECK(&FaultInjector::instance() == &FaultInjector::instance());
}

TEST_CASE("arena is empty when the backing's construction probe fails") {
    FaultInjector injector;
    FaultInjectingAllocator backing{injector};
    injector.enqueue("backing.allocate", 0, CoreError::BACKING_ALLOCATOR_FAILED);

    infinity::core::ArenaAllocator arena{1024, backing};

    CHECK(arena.capacityBytes() == 0);
    CHECK(arena.usedBytes() == 0);
    CHECK(arena.allocate(8, 1) == nullptr);
    CHECK(arena.allocate(1, 16) == nullptr);
    CHECK_FALSE(arena.supportsAlignment(8));
    CHECK(backing.lastFailureName() == BACKING_ALLOCATOR_FAILED);

    arena.reset();
    CHECK(arena.capacityBytes() == 0);
    CHECK(backing.deallocationCount() == 0);
}

TEST_CASE("pool is empty when the backing's construction probe fails") {
    FaultInjector injector;
    FaultInjectingAllocator backing{injector};
    injector.enqueue("backing.allocate", 0, CoreError::BACKING_ALLOCATOR_FAILED);

    infinity::core::PoolAllocator pool{16, alignof(std::max_align_t), 4, backing};

    CHECK(pool.capacity() == 0);
    CHECK(pool.usedCount() == 0);
    CHECK(pool.allocate(16, alignof(std::max_align_t)) == nullptr);
    CHECK_FALSE(pool.supportsAlignment(alignof(std::max_align_t)));
    CHECK_FALSE(pool.supportsAlignment(1));
    CHECK(backing.lastFailureName() == BACKING_ALLOCATOR_FAILED);
    CHECK(backing.deallocationCount() == 0);
}

TEST_CASE("injector state persists across objects: a later arena sees the failure") {
    FaultInjector injector;
    FaultInjectingAllocator backing{injector};

    {
        infinity::core::ArenaAllocator first{1024, backing};
        CHECK(first.capacityBytes() == 1024);
        CHECK(backing.lastFailureName() == NONE);
        CHECK(first.allocate(16, alignof(std::max_align_t)) != nullptr);

        injector.failNext("backing.allocate", CoreError::BACKING_ALLOCATOR_FAILED);
        infinity::core::ArenaAllocator second{1024, backing};
        CHECK(second.capacityBytes() == 0);
        CHECK(second.allocate(8, 1) == nullptr);
        CHECK(backing.lastFailureName() == BACKING_ALLOCATOR_FAILED);
    }
    CHECK(backing.deallocationCount() == 1);
}
