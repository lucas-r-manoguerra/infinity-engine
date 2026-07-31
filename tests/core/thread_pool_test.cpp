// tests/core/thread_pool_test.cpp
//
// ThreadPool contract tests (F2.7 part 1, rules 06/11). BASIC functional
// coverage: single and batched execution, worker count, destructor drain,
// reuse across batches, queue-depth reporting, non-blocking trySubmit,
// blocking submit bounded by queue capacity, and a light multi-worker stress
// run. The advanced concurrency/edge suite (barrier and shutdown races,
// multi-submitter stress) is a separate follow-up task.
//
// The pool makes NO execution-order guarantee (rule 11): these cases assert
// only that accepted tasks run exactly once and that waitAll() is a correct
// barrier - never the order in which they ran.
//
// Concurrency note: the blocking-submit and queue-depth cases use a test gate
// (Blocker below) to deterministically pin the single worker inside a task,
// so the queue state they assert is exact, not timing-dependent. The only
// sleep is a small "give the submitter a chance to enter the wait" pause that
// cannot flip a result the other way: the condition it checks is guaranteed
// by the queue capacity and the pinned worker, not by the sleep.
#include "infinity/core/thread_pool.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

#include <doctest/doctest.h>

namespace {

using infinity::core::ThreadPool;

constexpr uint32_t TASK_COUNT = 1000;
constexpr uint32_t STRESS_TASK_COUNT = 5000;
constexpr uint32_t STRESS_WORKERS = 4;
constexpr size_t STRESS_CAPACITY = 128;
constexpr uint32_t BLOCKING_POOL_CAPACITY = 4;
// countTask payloads submitted in the blocking case: BLOCKING_POOL_CAPACITY
// fill the queue while the worker is pinned, plus one that must wait for a
// slot to free.
constexpr uint32_t BLOCKING_PAYLOAD_TOTAL = BLOCKING_POOL_CAPACITY + 1;
// Loose upper bound (seconds) for 4 workers x 5000 trivial increments: tens of
// milliseconds in practice, wide headroom so a loaded CI runner never trips it
// (same pattern as diagnostics_test.cpp).
constexpr double STRESS_SECONDS_BOUND = 5.0;

// Task that increments the atomic pointed to by userData.
void countTask(void* userData) noexcept {
    std::atomic<uint32_t>& counter = *static_cast<std::atomic<uint32_t>*>(userData);
    ++counter;
}

// Test gate that pins ONE worker inside a task until the test releases it.
// started is set under the mutex before the task waits, so the test can
// deterministically know the task has begun running.
struct Blocker {
    std::mutex mutex;
    std::condition_variable cv;
    bool started{false};
    bool released{false};
};

// Holds the worker hostage: marks started, then waits until released. Used to
// keep the single worker busy so the queue state in a test is deterministic.
void blockingTask(void* userData) noexcept {
    Blocker& blocker = *static_cast<Blocker*>(userData);
    std::unique_lock<std::mutex> lock(blocker.mutex);
    blocker.started = true;
    blocker.cv.wait(lock, [&blocker] { return blocker.released; });
}

} // namespace

TEST_CASE("spawn runs a single submitted task exactly once") {
    ThreadPool pool{1};
    std::atomic<uint32_t> counter{0};

    pool.submit(countTask, &counter);
    pool.waitAll();

    CHECK(counter.load() == 1);
}

TEST_CASE("all N submitted tasks run exactly once") {
    ThreadPool pool{2};
    std::atomic<uint32_t> counter{0};

    for (uint32_t i = 0; i < TASK_COUNT; ++i) {
        pool.submit(countTask, &counter);
    }
    pool.waitAll();

    CHECK(counter.load() == TASK_COUNT);
}

TEST_CASE("workerCount matches the constructor argument") {
    const ThreadPool pool{4};

    CHECK(pool.workerCount() == 4);
    CHECK(pool.pendingCount() == 0);
}

TEST_CASE("destructor drains pending tasks before returning") {
    std::atomic<uint32_t> counter{0};
    {
        ThreadPool pool{2};
        for (uint32_t i = 0; i < 5; ++i) {
            pool.submit(countTask, &counter);
        }
    }

    CHECK(counter.load() == 5);
}

TEST_CASE("pool runs further batches after waitAll") {
    ThreadPool pool{2};
    std::atomic<uint32_t> counter{0};

    for (uint32_t i = 0; i < 10; ++i) {
        pool.submit(countTask, &counter);
    }
    pool.waitAll();
    CHECK(counter.load() == 10);

    for (uint32_t i = 0; i < 10; ++i) {
        pool.submit(countTask, &counter);
    }
    pool.waitAll();

    CHECK(counter.load() == 20);
}

TEST_CASE("pendingCount tracks queued work and drops to zero after waitAll") {
    Blocker blocker;
    ThreadPool pool{1, 8};
    std::atomic<uint32_t> counter{0};

    pool.submit(blockingTask, &blocker);
    while (!blocker.started) {
        std::this_thread::yield();
    }

    pool.submit(countTask, &counter);
    // The single worker is pinned inside the blocking task, so the payload is
    // still queued: the depth here is exact, not approximate.
    CHECK(pool.pendingCount() == 1);

    {
        std::lock_guard<std::mutex> lock(blocker.mutex);
        blocker.released = true;
    }
    blocker.cv.notify_all();
    pool.waitAll();

    CHECK(pool.pendingCount() == 0);
    CHECK(counter.load() == 1);
}

TEST_CASE("trySubmit accepts work with spare capacity and the task runs") {
    ThreadPool pool{2, 8};
    std::atomic<uint32_t> counter{0};

    CHECK(pool.trySubmit(countTask, &counter));
    CHECK(pool.trySubmit(countTask, &counter));
    pool.waitAll();

    CHECK(counter.load() == 2);
}

TEST_CASE("blocking submit is bounded by queue capacity and never deadlocks") {
    Blocker blocker;
    ThreadPool pool{1, BLOCKING_POOL_CAPACITY};
    std::atomic<uint32_t> counter{0};

    // The first task pins the only worker; afterwards the queue is the only
    // place tasks can wait, so its depth is exact.
    pool.submit(blockingTask, &blocker);
    while (!blocker.started) {
        std::this_thread::yield();
    }

    for (uint32_t i = 0; i < BLOCKING_POOL_CAPACITY; ++i) {
        pool.submit(countTask, &counter);
    }
    CHECK(pool.pendingCount() == BLOCKING_POOL_CAPACITY);

    // One more payload exceeds capacity: this submit MUST block on the slot CV
    // until the blocker is released. Total tasks: 1 blocker + capacity + 1
    // payloads; total payload runs: BLOCKING_PAYLOAD_TOTAL.
    std::atomic<bool> lastSubmitReturned{false};
    std::thread submitter{[&] {
        pool.submit(countTask, &counter);
        lastSubmitReturned.store(true);
    }};
    // Let the submitter reach the slot wait. The check below cannot flake:
    // while the queue is full and the worker is pinned, the submit cannot
    // return no matter how the threads interleave.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK_FALSE(lastSubmitReturned.load());

    {
        std::lock_guard<std::mutex> lock(blocker.mutex);
        blocker.released = true;
    }
    blocker.cv.notify_all();

    submitter.join();
    CHECK(lastSubmitReturned.load());
    pool.waitAll();

    CHECK(counter.load() == BLOCKING_PAYLOAD_TOTAL);
}

TEST_CASE("basic stress: 4 workers run 5000 tasks exactly once within bounds") {
    ThreadPool pool{STRESS_WORKERS, STRESS_CAPACITY};
    std::atomic<uint32_t> counter{0};
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    for (uint32_t i = 0; i < STRESS_TASK_COUNT; ++i) {
        pool.submit(countTask, &counter);
    }
    pool.waitAll();

    const double elapsedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(counter.load() == STRESS_TASK_COUNT);
    CHECK(elapsedSeconds < STRESS_SECONDS_BOUND);
}
