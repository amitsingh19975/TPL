#ifndef AMT_TPL_WORKER_POOL_HPP
#define AMT_TPL_WORKER_POOL_HPP

#include <vector>
#include "thread.hpp"
#include "waiter.hpp"

namespace tpl {

    struct Scheduler;

    struct WorkerPool {
        using thread_t = std::thread;

        WorkerPool(
            Scheduler& schedular,
            std::size_t nthreads = tpl::hardware_max_parallism()
        ) : m_parent(schedular)
        {
            for (auto i = 0ul; i < nthreads; ++i) {
                m_threads.emplace_back(&WorkerPool::do_work, this, i);
            }
        }
        WorkerPool(WorkerPool const&) = delete;
        WorkerPool(WorkerPool &&) = delete;
        WorkerPool& operator=(WorkerPool const&) = delete;
        WorkerPool& operator=(WorkerPool &&) = delete;
        ~WorkerPool() {
            stop();
        }

        auto stop() -> void {
            waiter.notify_all([this] {
                m_is_running.store(false);
            });
            for(auto& t: m_threads) {
                t.join();
            }
        }

        constexpr auto is_running() const noexcept { return m_is_running.load(); }

        internal::Waiter waiter;
    private:
        friend struct TaskToken;
        void do_work(std::size_t thread_id);
    private:
        std::vector<thread_t> m_threads;
        std::atomic<bool> m_is_running{true};
        Scheduler& m_parent;
    };
} // namespace tpl

#endif // AMT_TPL_WORKER_POOL_HPP
