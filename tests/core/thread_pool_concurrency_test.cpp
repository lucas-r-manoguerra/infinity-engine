// tests/core/thread_pool_concurrency_test.cpp
//
// ThreadPool ADVANCED concurrency/edge suite (F2.7 part 2, rules 06/11). The
// basic functional suite lives in thread_pool_test.cpp (9 cases); this TU
// extends it with the barrier, shutdown, self-submit, stopped-pool and
// multi-submitter scenarios the basic file deliberately left for a follow-up.
// Split into a second file because the combined TU would exceed the ~400-line
// test-file guideline ("One File = One Task", rule 01).
//
// Same conventions as the basic suite: CHECK (never REQUIRE), std::atomic for
// shared counters, the Blocker gate for deterministic worker pinning, and
// gates/joins as the primary synchronization. No execution-order guarantee is
// asserted (rule 11) - only that accepted tasks run exactly once and waitAll()
// is a correct snapshot barrier.
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
#include <set>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

namespace {

using infinity::core::ThreadPool;

constexpr uint32_t CONCURRENCY_WORKERS = 4;
constexpr unsigned MIN_CORES_FOR_PARALLELISM = 2;

constexpr uint32_t QUEUE_FULL_CAPACITY = 2;
constexpr uint32_t QUEUE_FULL_WORKERS = 1;

constexpr uint32_t SNAPSHOT_WORKERS = 4;
constexpr size_t SNAPSHOT_CAPACITY = 256;
constexpr uint32_t SNAPSHOT_SUBMITS_PER_THREAD = 200;

constexpr uint32_t DRAIN_WORKERS = 2;
constexpr uint32_t DRAIN_HELPER_TASKS = 50;

constexpr uint32_t MIXED_STRESS_WORKERS = 4;
constexpr size_t MIXED_STRESS_CAPACITY = 128;
constexpr uint32_t MIXED_STRESS_SUBMITTERS = 3;
constexpr uint32_t MIXED_STRESS_SUBMITS_PER_SUBMITTER = 2000;
// Loose upper bound (seconds) for 3 submitters x 2000 trivial increments: tens
// of milliseconds in practice, wide headroom so a loaded CI runner never trips
// it (same pattern as the basic suite).
constexpr double MIXED_STRESS_SECONDS_BOUND = 5.0;

constexpr uint32_t REUSE_BATCH_SIZE = 10;
// Hang-detector for the idempotent waitAll call: repeated barrier plus 20
// trivial tasks is sub-millisecond; this bound only catches a deadlock.
constexpr double REUSE_SECONDS_BOUND = 5.0;

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

// Payload shared by the barrier tasks: a mutex-protected set of distinct
// worker thread ids plus the barrier state. The gate proves parallelism BY
// CONSTRUCTION, not by timing: task A blocks its worker until task B arrives,
// so B must run on a DIFFERENT worker thread (the one running A is pinned and
// cannot take B). The gate self-releases when both tasks have arrived, so
// waitAll() after the submits can never deadlock.
struct ThreadBarrierRecorder {
    std::mutex mutex;
    std::condition_variable cv;
    std::set<std::thread::id> ids;
    uint32_t arrived{0};
};

// Barrier task: records the worker's thread id, then blocks until both tasks
// have arrived (or a hang-detector timeout). Two such tasks can only both be
// running at once if two distinct worker threads execute them.
void barrierThreadIdTask(void* userData) noexcept {
    ThreadBarrierRecorder& recorder = *static_cast<ThreadBarrierRecorder*>(userData);
    std::unique_lock<std::mutex> lock(recorder.mutex);
    recorder.ids.insert(std::this_thread::get_id());
    ++recorder.arrived;
    if (recorder.arrived >= 2) {
        recorder.cv.notify_all();
    } else {
        recorder.cv.wait_for(lock, std::chrono::seconds(5),
                             [&recorder] { return recorder.arrived >= 2; });
    }
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

TEST_CASE("concurrency reality: two barrier tasks run on distinct worker threads") {
    // The barrier proves parallelism without timing assumptions: task A pins
    // its worker until task B arrives, so B is forced onto a second worker
    // thread. Previously this suite used 8 trivial tasks and asserted
    // ids.size() >= 2, which flaked under `cmake --preset ci` because one
    // worker could drain the whole queue before the others woke (rule 06: a
    // flaky test is fixed or removed, never ignored).
    if (std::thread::hardware_concurrency() < MIN_CORES_FOR_PARALLELISM) {
        return;
    }

    ThreadPool pool{CONCURRENCY_WORKERS};
    ThreadBarrierRecorder recorder;

    pool.submit(barrierThreadIdTask, &recorder);
    pool.submit(barrierThreadIdTask, &recorder);
    // The gate self-releases once both tasks have arrived, so this waitAll
    // cannot deadlock and cannot return while a task is pinned.
    pool.waitAll();

    CHECK(recorder.ids.size() >= 2);
    CHECK(recorder.arrived == 2);
}

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

TEST_CASE("waitAll is a snapshot barrier for concurrent submitters") {
    ThreadPool pool{SNAPSHOT_WORKERS, SNAPSHOT_CAPACITY};
    std::atomic<uint32_t> counter{0};

    // Both submitters must COMPLETE before waitAll is called (the barrier
    // snapshot contract, thread_pool.h): a submit that races with the call may
    // or may not be included, so the join is part of the test, not an accident.
    std::thread submitterA{[&pool, &counter] {
        for (uint32_t i = 0; i < SNAPSHOT_SUBMITS_PER_THREAD; ++i) {
            pool.submit(countTask, &counter);
        }
    }};
    std::thread submitterB{[&pool, &counter] {
        for (uint32_t i = 0; i < SNAPSHOT_SUBMITS_PER_THREAD; ++i) {
            pool.submit(countTask, &counter);
        }
    }};
    submitterA.join();
    submitterB.join();

    pool.waitAll();

    CHECK(counter.load() == 2 * SNAPSHOT_SUBMITS_PER_THREAD);
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
    SelfSubmitPayload payload{&pool, &counter};

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

TEST_CASE("stress under mixed ops: 3 submitters x 2000 increments, retrying on a full queue") {
    ThreadPool pool{MIXED_STRESS_WORKERS, MIXED_STRESS_CAPACITY};
    std::atomic<uint32_t> counter{0};
    constexpr uint32_t TOTAL_SUBMITS = MIXED_STRESS_SUBMITTERS * MIXED_STRESS_SUBMITS_PER_SUBMITTER;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    // Each submitter retries trySubmit with a yield when the queue is full.
    // Progress is guaranteed: the 4 workers keep draining, so a slot always
    // frees and no submitter livelocks.
    std::vector<std::thread> submitters;
    submitters.reserve(MIXED_STRESS_SUBMITTERS);
    for (uint32_t t = 0; t < MIXED_STRESS_SUBMITTERS; ++t) {
        submitters.emplace_back([&pool, &counter] {
            uint32_t accepted = 0;
            while (accepted < MIXED_STRESS_SUBMITS_PER_SUBMITTER) {
                if (pool.trySubmit(countTask, &counter)) {
                    ++accepted;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    for (std::thread& submitter : submitters) {
        submitter.join();
    }
    pool.waitAll();

    const double elapsedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(counter.load() == TOTAL_SUBMITS);
    CHECK(elapsedSeconds < MIXED_STRESS_SECONDS_BOUND);
}

TEST_CASE("waitAll is idempotent and the pool stays reusable after a second waitAll") {
    ThreadPool pool{2};
    std::atomic<uint32_t> counter{0};
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    for (uint32_t i = 0; i < REUSE_BATCH_SIZE; ++i) {
        pool.submit(countTask, &counter);
    }
    pool.waitAll();
    // A second, immediate waitAll must return promptly: the barrier predicate
    // (m_active == 0 && m_size == 0) is already true, so it cannot block.
    pool.waitAll();
    CHECK(counter.load() == REUSE_BATCH_SIZE);

    for (uint32_t i = 0; i < REUSE_BATCH_SIZE; ++i) {
        pool.submit(countTask, &counter);
    }
    pool.waitAll();

    const double elapsedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(counter.load() == 2 * REUSE_BATCH_SIZE);
    CHECK(elapsedSeconds < REUSE_SECONDS_BOUND);
}
