// tests/core/thread_pool_barrier_test.cpp
//
// ThreadPool BARRIER/PARALLELISM test suite (F2.7 part 2, rules 06/11). Split
// from thread_pool_concurrency_test.cpp: this TU covers the barrier cases -
// real parallelism across workers, waitAll() as a snapshot barrier for
// concurrent submitters, multi-submitter stress with retry on a full queue,
// and waitAll() idempotence with pool reuse. The queue/lifecycle cases
// (trySubmit, blocking submit, destructor drain, self-submit, stopped pool)
// live in thread_pool_queue_test.cpp (rule 01: One File = One Task).
//
// Same conventions as the basic suite: CHECK (never REQUIRE), std::atomic for
// shared counters, the barrier gate for deterministic worker pinning, and
// gates/joins as the primary synchronization. No execution-order guarantee is
// asserted (rule 11) - only that accepted tasks run exactly once and waitAll()
// is a correct snapshot barrier.
//
// FLAKE-FREE POLICY (rule 06): synchronization here is always a gate or a
// join. Timing bounds are generous hang-detectors, never precision
// assertions.
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

constexpr uint32_t SNAPSHOT_WORKERS = 4;
constexpr size_t SNAPSHOT_CAPACITY = 256;
constexpr uint32_t SNAPSHOT_SUBMITS_PER_THREAD = 200;

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
