// infinity/core/thread_pool.h
//
// Fixed-worker ThreadPool (F2.7, rules 03/08/11). A bounded pool of worker
// threads created once at construction; workers block when idle (condition
// variables, never busy-wait) and tasks are submitted from any thread.
//
//   Queue         - A fixed ring buffer of capacity slots, pre-allocated at
//                   construction and never resized: submit/trySubmit allocate
//                   nothing (rule 08: 0 allocations after init). The default
//                   capacity is DEFAULT_QUEUE_CAPACITY (256).
//   Task          - A function pointer plus an opaque context pointer. No
//                   std::function, no per-submit heap traffic (rule 08); the
//                   engine owns the payload lifetime through userData.
//   Blocking      - submit() blocks until a slot frees; trySubmit() is the
//                   non-blocking variant, false when the queue is full (a
//                   recoverable, caller-decided condition - rule 04).
//   Barrier       - waitAll() is a SNAPSHOT barrier. It first acquires the
//                   internal lock, so no submit that happens-before the call
//                   can add work after the check; it then blocks until every
//                   task submitted before it has RUN and the queue is empty.
//                   Tasks submitted concurrently by other threads after the
//                   lock is taken may or may not be included. Callers that
//                   need a strict barrier must submit from a single thread.
//   Shutdown      - The destructor sets the stop flag, wakes every worker and
//                   joins. Workers DRAIN the queue before exiting: a task is
//                   never silently dropped. Shutdown is graceful, never
//                   preemptive.
//   Determinism   - (rule 11) The pool is a SCHEDULER: it makes NO
//                   execution-order guarantee. Guaranteed: (1) a task accepted
//                   by submit/trySubmit runs exactly once; (2) waitAll() is a
//                   correct barrier for the tasks submitted before it.
//                   Engine-level determinism comes from barriers plus future
//                   read/write sets, not from pool ordering.
//   No global state- Every field lives in the instance; the pool shares no
//                   static or process-wide state (rule 11).
//   Constraints   - workerCount 0 is a programming error: asserted in debug,
//                   clamped to 1 in release. queueCapacity 0 clamps to 1.
//                   waitAll() must not be called from inside a task running on
//                   the pool (self-deadlock). Submitting to a stopped pool is
//                   a programming error: asserted in debug, a no-op in
//                   release. Worker-thread creation failure terminates (the
//                   engine is -fno-exceptions; this only happens when the OS
//                   cannot create a thread at all).
//
// Lifetime: the pool owns its threads; copy and move are deleted. userData
// must stay valid until the task has run (waitAll or destruction), because
// the task may still be queued when the submit call returns.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace infinity::core {

// Default queue capacity (slots) when the constructor does not specify one.
// The buffer is pre-allocated once at construction: this is fixed memory, not
// a soft limit.
inline constexpr size_t DEFAULT_QUEUE_CAPACITY = 256;

// Bounded, fixed-worker task pool. See the file brief for the queue, barrier,
// shutdown and determinism contracts.
class ThreadPool {
public:
    // Task signature: a plain function pointer plus an opaque context. Cheap
    // to pass and store; the payload lifetime is the caller's (see the brief).
    using Task = void (*)(void* userData) noexcept;

    // Spawns workerCount workers with a DEFAULT_QUEUE_CAPACITY-slot queue.
    explicit ThreadPool(uint32_t workerCount);

    // Spawns workerCount workers with a queueCapacity-slot queue.
    ThreadPool(uint32_t workerCount, size_t queueCapacity);

    ThreadPool() = delete;
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Graceful drain-then-join (see the brief): set stop, wake all workers,
    // workers finish the remaining queue, then every worker thread is joined.
    ~ThreadPool();

    // Enqueues task without blocking. False when the queue is full or the pool
    // is stopped; the caller decides whether to retry or degrade. On true the
    // task is guaranteed to run exactly once.
    bool trySubmit(Task task, void* userData) noexcept;

    // Enqueues task, blocking until a slot frees. No-op when the pool is
    // stopped (asserted in debug: rule 04). Accepted tasks run exactly once.
    void submit(Task task, void* userData) noexcept;

    // Snapshot barrier (see the brief): blocks until every task submitted
    // before the call has run and the queue is empty.
    void waitAll() noexcept;

    [[nodiscard]] uint32_t workerCount() const noexcept;
    // Approximate number of tasks waiting in the queue (a snapshot under the
    // internal lock; a running task is not counted).
    [[nodiscard]] uint32_t pendingCount() const noexcept;
    [[nodiscard]] bool isStopped() const noexcept;

private:
    struct TaskItem {
        Task fn{nullptr};
        void* data{nullptr};
    };

    void workerLoop() noexcept;

    // Ring-buffer queue: m_queue.size() is the capacity (fixed at
    // construction); [m_head, m_tail) holds the m_size pending items.
    std::vector<std::optional<TaskItem>> m_queue;
    uint32_t m_workerCount{0};
    size_t m_head{0};
    size_t m_tail{0};
    size_t m_size{0};
    uint32_t m_active{0}; // tasks currently executing (outside the lock)
    bool m_stopped{false};
    mutable std::mutex m_mutex;
    std::condition_variable m_workCv;    // work available - workers wait here
    std::condition_variable m_slotCv;    // a slot freed - submitters wait here
    std::condition_variable m_barrierCv; // active hit zero - waitAll waits here
    std::vector<std::thread> m_workers;
};

} // namespace infinity::core
