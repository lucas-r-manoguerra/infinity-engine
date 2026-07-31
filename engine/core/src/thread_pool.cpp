// src/thread_pool.cpp
#include "infinity/core/thread_pool.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace infinity::core {

ThreadPool::ThreadPool(uint32_t workerCount) : ThreadPool(workerCount, DEFAULT_QUEUE_CAPACITY) {}

ThreadPool::ThreadPool(uint32_t workerCount, size_t queueCapacity) {
    assert(workerCount > 0);
    assert(queueCapacity > 0);
    m_workerCount = std::max<uint32_t>(workerCount, 1);
    m_queue.resize(std::max<size_t>(queueCapacity, 1));
    m_workers.reserve(m_workerCount);
    for (uint32_t i = 0; i < m_workerCount; ++i) {
        m_workers.emplace_back(&ThreadPool::workerLoop, this);
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_stopped = true;
    }
    m_workCv.notify_all();
    m_slotCv.notify_all();
    m_barrierCv.notify_all();
    for (std::thread& worker : m_workers) {
        worker.join();
    }
}

bool ThreadPool::trySubmit(Task task, void* userData) noexcept {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopped || m_size == m_queue.size()) {
            return false;
        }
        m_queue[m_tail] = TaskItem{.fn = task, .data = userData};
        m_tail = (m_tail + 1) % m_queue.size();
        ++m_size;
    }
    m_workCv.notify_one();
    return true;
}

void ThreadPool::submit(Task task, void* userData) noexcept {
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_slotCv.wait(lock, [this] { return m_size < m_queue.size() || m_stopped; });
        assert(!m_stopped); // submitting to a stopped pool is a programming error (rule 04)
        if (m_stopped) {
            return;
        }
        m_queue[m_tail] = TaskItem{.fn = task, .data = userData};
        m_tail = (m_tail + 1) % m_queue.size();
        ++m_size;
    }
    m_workCv.notify_one();
}

void ThreadPool::waitAll() noexcept {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_barrierCv.wait(lock, [this] { return m_active == 0 && m_size == 0; });
}

uint32_t ThreadPool::workerCount() const noexcept { return m_workerCount; }

uint32_t ThreadPool::pendingCount() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<uint32_t>(m_size);
}

bool ThreadPool::isStopped() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stopped;
}

void ThreadPool::workerLoop() noexcept {
    for (;;) {
        TaskItem item;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_workCv.wait(lock, [this] { return m_size > 0 || m_stopped; });
            if (m_stopped && m_size == 0) {
                return; // drain finished: no work left, pool is stopping
            }
            // guaranteed: the wait predicate (m_size > 0) implies this slot is engaged
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            item = m_queue[m_head].value();
            m_queue[m_head].reset();
            m_head = (m_head + 1) % m_queue.size();
            --m_size;
            ++m_active;
            m_slotCv.notify_one();
        }
        // User code runs OUTSIDE the lock (rule: never hold the mutex while
        // running a task - a blocking task must not stall every submitter).
        item.fn(item.data);
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            --m_active;
            if (m_active == 0) {
                m_barrierCv.notify_all();
            }
        }
    }
}

} // namespace infinity::core
