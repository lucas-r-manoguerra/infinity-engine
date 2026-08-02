// tests/core/thread_pool_queue_test.cpp
//
// ThreadPool QUEUE/LIFECYCLE test suite (F2.7 part 2, rules 06/11). Split
// from thread_pool_concurrency_test.cpp: this TU covers the queue and
// lifecycle edges - trySubmit on a full queue, blocking submit waiting for a
// slot, destructor drain under a helper submitter, single-level self-submit,
// and the stopped-pool drain contract. The barrier/parallelism cases
// (concurrency reality, waitAll snapshot, mixed stress, waitAll idempotence)
// live in thread_pool_barrier_test.cpp (rule 01: One File = One Task).
//
// Same conventions as the basic suite: CHECK (never REQUIRE), std::atomic for
// shared counters, the Blocker gate for deterministic worker pinning, and
// gates/joins as the primary synchronization.
//
// FLAKE-FREE POLICY (rule 06): synchronization here is always a gate or a
// join; the only sleep is a negative assertion ("the submit has NOT returned")
// whose falseness is structurally guaranteed by the full queue and the pinned
// worker, not by the sleep. Timing bounds are generous hang-detectors, never
// precision assertions.
#include "infinity/core/thread_pool.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include <doctest/doctest.h>

namespace {

using infinity::core::ThreadPool;

constexpr uint32_t QUEUE_FULL_CAPACITY = 2;
constexpr uint32_t QUEUE_FULL_WORKERS = 1;

constexpr uint32_t DRAIN_WORKERS = 2;
constexpr uint32_t DRAIN_HELPER_TASKS = 50;

// Task that increments the atomic pointed to by userData.
void countTask(void* userData) noexcept {
    std::atomic<uint32_t>& counter = *static_cast<std::atomic<uint32_t>*>(userData);
    ++counter;
}

// Test gate that pins ONE worker inside a task until the test releases it.
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

// Payload for selfSubmitTask: the pool to submit into and the counter.
struct SelfSubmitPayload {
    ThreadPool* pool;
    std::atomic<uint32_t>* counter;
};

// Task that submits ONE more task to the same pool and returns. It must NOT
// call waitAll: waitAll from inside a task would self-deadlock (thread_pool.h
// constraint) because the running task keeps m_active > 0 until it returns.
void selfSubmitTask(void* userData) noexcept {
    SelfSubmitPayload& payload = *static_cast<SelfSubmitPayload*>(userData);
    payload.pool->submit(countTask, payload.counter);
}

} // namespace

TEST_CASE("trySubmit reports false when the queue is full and the worker is busy") {
    Blocker blocker;
    ThreadPool pool{QUEUE_FULL_WORKERS, QUEUE_FULL_CAPACITY};
    std::atomic<uint32_t> counter{0};

    // Pin the single worker inside the blocking task so the queue is the only
    // place tasks can wait (same gate as the basic suite).
    pool.submit(blockingTask, &blocker);
    while (!blocker.started) {
        std::this_thread::yield();
    }

    // Two accepted payloads fill the two free slots exactly.
    CHECK(pool.trySubmit(countTask, &counter));
    CHECK(pool.trySubmit(countTask, &counter));
    // Third: no slot free and the worker is pinned - the queue is full.
    CHECK_FALSE(pool.trySubmit(countTask, &counter));

    {
        std::lock_guard<std::mutex> lock(blocker.mutex);
        blocker.released = true;
    }
    blocker.cv.notify_all();
    pool.waitAll();

    CHECK(counter.load() == 2);

    // The worker drained the queue, so a slot is free again.
    CHECK(pool.trySubmit(countTask, &counter));
    pool.waitAll();
    CHECK(counter.load() == 3);
}

TEST_CASE("blocking submit waits for a free slot and then runs (bounded, no deadlock)") {
    Blocker blocker;
    ThreadPool pool{QUEUE_FULL_WORKERS, QUEUE_FULL_CAPACITY};
    std::atomic<uint32_t> counter{0};

    pool.submit(blockingTask, &blocker);
    while (!blocker.started) {
        std::this_thread::yield();
    }

    CHECK(pool.trySubmit(countTask, &counter));
    CHECK(pool.trySubmit(countTask, &counter));

    // Blocking submit of the third payload must wait for the worker to free a
    // slot. The worker runs user code OUTSIDE the pool lock (thread_pool.cpp),
    // so a blocked submitter cannot starve the worker: the wait is bounded by
    // the drain that follows the release, never a deadlock.
    std::atomic<bool> submitterReturned{false};
    std::thread submitter{[&pool, &counter, &submitterReturned] {
        pool.submit(countTask, &counter);
        submitterReturned.store(true);
    }};
    // Negative assertion that cannot flake: with the queue full and the worker
    // pinned, the submit cannot return regardless of thread interleaving.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK_FALSE(submitterReturned.load());

    {
        std::lock_guard<std::mutex> lock(blocker.mutex);
        blocker.released = true;
    }
    blocker.cv.notify_all();

    submitter.join();
    CHECK(submitterReturned.load());
    pool.waitAll();

    // 2 trySubmit payloads + 1 blocking payload.
    CHECK(counter.load() == 3);
}

TEST_CASE("destructor drains tasks submitted concurrently by a helper thread") {
    std::atomic<uint32_t> counter{0};
    std::atomic<bool> submitsDone{false};
    std::thread helper;
    {
        ThreadPool pool{DRAIN_WORKERS};

        // The handshake is what makes this deterministic: the helper finishes
        // ALL submits before the scope exits, so no submit races with the
        // destructor (submitting to a stopped pool is asserted in debug). The
        // destructor then drains whatever is queued or in flight.
        helper = std::thread{[&pool, &counter, &submitsDone] {
            for (uint32_t i = 0; i < DRAIN_HELPER_TASKS; ++i) {
                pool.submit(countTask, &counter);
            }
            submitsDone.store(true);
        }};
        while (!submitsDone.load()) {
            std::this_thread::yield();
        }
    } // destructor runs here: stops the pool and drains the DRAIN_HELPER_TASKS

    // Join before checking: the helper's submits happen-before the destructor,
    // which joins the workers, so every increment is visible here.
    helper.join();

    CHECK(counter.load() == DRAIN_HELPER_TASKS);
}

TEST_CASE("a task can submit one more task to the same pool (single-level self-submit)") {
    // One worker: proves the inner submit does not require a second worker to
    // make progress (the worker runs user code outside the pool lock).
    ThreadPool pool{1};
    std::atomic<uint32_t> counter{0};
    SelfSubmitPayload payload{.pool = &pool, .counter = &counter};

    pool.submit(selfSubmitTask, &payload);
    pool.waitAll(); // covers both the outer task and the inner one

    CHECK(counter.load() == 1);
}

TEST_CASE("stopped pool is only reachable through destruction, which drains every task") {
    // Contract (thread_pool.h): trySubmit returns false when the pool is
    // stopped; blocking submit to a stopped pool is a no-op in release and
    // asserted in debug. The pool has NO public stop() - the stopped state is
    // reachable only through the destructor - and calling a member on a
    // destroyed object is use-after-free (UB), so those reject branches cannot
    // be exercised from a live object. What IS observable is the other half of
    // the contract: destruction both stops the pool and drains every accepted
    // task; a task is never silently dropped.
    std::atomic<uint32_t> counter{0};
    {
        ThreadPool pool{QUEUE_FULL_WORKERS, QUEUE_FULL_CAPACITY};
        pool.submit(countTask, &counter);
        pool.submit(countTask, &counter);
    } // destructor: stop + drain

    CHECK(counter.load() == 2);
}
