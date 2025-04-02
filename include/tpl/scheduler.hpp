#ifndef AMT_TPL_SCHEDULAR_HPP
#define AMT_TPL_SCHEDULAR_HPP

#include "signal_tree/tree.hpp"
#include "task.hpp"
#include "signal_tree/int.hpp"
#include "thread.hpp"
#include "list.hpp"
#include "queue.hpp"
#include "awaiter.hpp"
#include "waiter.hpp"
#include "worker_pool.hpp"
#include "task_token.hpp"
#include "atomic.hpp"
#include "value_store.hpp"
#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <print>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>
#include <span>
#include "task_id.hpp"

#ifdef __cpp_exceptions
    #include <exception>
#endif

namespace tpl {

    enum class SchedulerError {
        no_root_task,
        cycle_found
    };

    constexpr auto to_string(SchedulerError e) noexcept -> std::string_view {
        switch (e) {
            case SchedulerError::no_root_task: return "There must be a root task that does not depends on any other tasks.";
            case SchedulerError::cycle_found: return "Cycle detected";
        }
    }

    struct Scheduler {
        static constexpr std::size_t capacity = internal::NodeIntTraits::max_nodes;
        using signal_tree = SignalTree<capacity>;

        Scheduler()
            : m_pool(*this)
        {}
        Scheduler(Scheduler const&) = delete;
        Scheduler(Scheduler &&) = delete;
        Scheduler& operator=(Scheduler const&) = delete;
        Scheduler& operator=(Scheduler &&) = delete;
        ~Scheduler() = default;
    private:
        friend struct TaskToken;
        friend struct WorkerPool;
        using queue_item_t = std::function<void()>;

        enum class TaskState: std::uint8_t {
            empty = 0,
            alive = 1,
            dead  = 2
        };
        struct TaskInfo {
            Task task;

            // INFO: It stores the exceptions that is thrown by task.
            // If it returns true any pending action will run; otherwise, it'll outright remove
            // the task from the queue.
            ErrorHandler error_handler;

            #ifdef __cpp_exceptions
                // INFO: Unhandled exceptions
                std::exception_ptr expception_ptr;
            #endif

            // INFO: Dependencies to signal when this completes
            std::vector<TaskId> dep_signals{};

            // INFO: Keeps track of inputs for verification
            std::vector<std::pair<TaskId, bool /*consumable*/>> inputs;
            // This can be non-atomic since we guarantee the task is owned by a single thread.
            bool has_signaled{false};

            // INFO: This indicates whether the value produced by this task
            // will be fed into multiple tasks. If set true, the value from
            // the value store will not be consumed by it'll be cloned.
            constexpr bool produces_immovable_value() const noexcept {
                return dep_signals.size() > 1;
            }

            // INFO: Since modifying vector will introduce the race condition,
            // we use this atomic. When this signal reaches to zeros, we
            // signal the queue to start the this task for processing.
            alignas(atomic::internal::hardware_destructive_interference_size) std::atomic<int> signals{};
            alignas(atomic::internal::hardware_destructive_interference_size) std::atomic<TaskState> state{TaskState::empty};

            TaskInfo(Task t, ErrorHandler h = ErrorHandler{}) noexcept
                : task(std::move(t))
                , error_handler(std::move(h))
                , state(TaskState::alive)
            {}

            TaskInfo() noexcept = default;
            TaskInfo(TaskInfo const&) = delete;
            TaskInfo(TaskInfo && other) noexcept
                : task(std::move(other.task))
                , error_handler(std::move(other.error_handler))
                , dep_signals(std::move(other.dep_signals))
                , inputs(std::move(other.inputs))
                , has_signaled(other.has_signaled)
                , signals(other.signals.load())
                , state(other.state.load())
            {}
            TaskInfo& operator=(TaskInfo const&) = delete;
            TaskInfo& operator=(TaskInfo && other) noexcept {
                if (this == &other) return *this;
                using std::swap;
                swap(task, other.task);
                swap(inputs, other.inputs);
                swap(has_signaled, other.has_signaled);
                swap(error_handler, other.error_handler);
                swap(dep_signals, other.dep_signals);
                signals.store(other.signals.load());
                state.store(other.state.load());
                return *this;
            }
            ~TaskInfo() = default;
        };

        auto set_signal(TaskId id) -> void {
            auto idx = tid_to_int(id);
            if (m_info[idx].state != TaskState::alive) return;
            auto [b, p] = parse_task_id(id);
            m_trees[b].set(p);
        }

        constexpr auto parse_task_id(
            TaskId id
        ) const noexcept -> std::pair<std::size_t, std::size_t> {
            return { tid_to_int(id) / capacity, tid_to_int(id) % capacity };
        }

        auto on_complete(TaskId id, bool should_set_state = false) {
            auto helper = [=, this] {
                auto& info = m_info[tid_to_int(id)];
                if (info.state != TaskState::alive) return;

                if (should_set_state) {
                    info.state = TaskState::empty;
                }

                if (info.has_signaled) return;
                info.has_signaled = true;


                for (auto i: info.dep_signals) {
                    auto& task = m_info[tid_to_int(i)];
                    // INFO: Atomic exchange is not required since we before hand know
                    // this should never go down to zero since we precomputed tasks
                    // that will reduce the count.
                    if (task.signals == 0) continue;
                    task.signals.fetch_sub(1);
                    if (task.signals == 0 && task.state == TaskState::alive) {
                        auto [b, p] = parse_task_id(i);
                        m_trees[b].set(p);
                        m_tasks.fetch_add(1);
                    }
                }
            };
            helper();

            complete_one_task();
            m_pool.waiter.notify_one();
            m_last_processed_task.store(id);
        }

        auto on_failure(TaskId id) {
            (void)id;
            complete_one_task();
        }

        auto on_reschedule(TaskId id) {
            (void)id;
            m_pool.waiter.notify_one();
        }

        auto build() -> std::expected<void, SchedulerError> {
            auto sz = m_trees.size();
            for (auto i = 0ul; i < sz; ++i) m_trees[i].clear();

            // 1. find incoming edges
            // Now we need to find nodes that do not have incoming edges
            std::vector<std::size_t> in_edges(m_info.size(), 0);
            m_info.for_each([&in_edges](auto& info) {
                // Need to consider both dead and alive
                if (info.state == TaskState::empty) return;
                for (auto dep: info.dep_signals) in_edges[tid_to_int(dep)]++;
            });

            // 2. queue tasks if there is no deps
            for (auto i = 0ul; i < in_edges.size(); ++i) {
                auto& info = m_info[i];
                if (info.state != TaskState::alive) continue;
                if (in_edges[i] == 0) {
                    set_signal(int_to_tid(i));
                    m_tasks.fetch_add(1);
                }
            }

            // 3. set consumable output values
            std::fill(in_edges.begin(), in_edges.end(), 0);
            auto& freq = in_edges;
            m_info.for_each([&freq](auto& info, std::size_t i) {
                if (info.state != TaskState::alive) return;
                freq[i] += info.dep_signals.size();
            });

            m_info.for_each([&freq](TaskInfo& info) {
                if (info.state != TaskState::alive) return;
                for (auto j = 0ul; j < info.inputs.size(); ++j) {
                    auto& in = info.inputs[j];
                    in.second = (freq[tid_to_int(in.first)] == 0);
                }
            });

            if (empty()) {
                return std::unexpected(SchedulerError::no_root_task);
            }
            return {};
        }

        auto set_error_handler(TaskId id, ErrorHandler&& handler) noexcept {
            m_info[tid_to_int(id)].error_handler = std::move(handler);
        }
    public:
        struct DependencyTracker {
            TaskId id;
            Scheduler* parent;

            auto deps_on(
                std::span<DependencyTracker> ids
            ) -> std::expected<void, SchedulerError>;

            template <typename... Ts>
                requires ((std::same_as<Ts, DependencyTracker> && ...) && (sizeof...(Ts) > 0))
            auto deps_on(Ts... ids) -> std::expected<void, SchedulerError> {
                std::array tmp { ids... };
                return deps_on(tmp);
            }

            auto set_error_handler(ErrorHandler handler) noexcept {
                parent->set_error_handler(id, std::move(handler));
            }
        };

        auto add_task(
            Task t,
            ErrorHandler handler
        ) -> DependencyTracker {
            ensure_space_for(m_info.size() + 1);

            auto sz = m_info.size();
            for (auto i = 0ul; i < sz; ++i) {
                auto& info = m_info[i];
                if (info.state == TaskState::alive) continue;
                m_info[i] = TaskInfo(std::move(t), std::move(handler));
                return { .id = int_to_tid(i), .parent = this };
            }
            std::unreachable();
        }

        template <typename Fn>
        constexpr auto add_task(
            Fn&& fn,
            Task::priority_t p = Task::priority_t::normal
        ) -> DependencyTracker {
            return add_task(
                Task(std::forward<Fn>(fn), p),
                ErrorHandler()
            );
        }

        template <typename Fn, typename EFn>
        constexpr auto add_task(
            Fn&& fn,
            EFn&& e_fn,
            Task::priority_t p = Task::priority_t::normal
        ) -> DependencyTracker {
            return add_task(
                Task(std::forward<Fn>(fn), p),
                ErrorHandler(std::forward<EFn>(e_fn))
            );
        }

        template <typename Fn>
            requires (std::is_nothrow_invocable_v<Fn>)
        auto awaitable_queue_work(
            Fn&& fn,
            Task::priority_t p = Task::priority_t::normal
        ) -> Awaiter<decltype(std::invoke(fn))> {
            using ret_t = decltype(std::invoke(fn));
            auto task = m_alloc->alloc<queue_item_t>();
            Awaiter<ret_t> await;
            new(task) queue_item_t(
                [wrapper = await.m_data, fn = std::forward<Fn>(fn), p] noexcept {
                    [[maybe_unused]] auto is_priority_set = ThisThread::set_priority(p);
                    assert(is_priority_set == true);
                    if constexpr (std::is_void_v<ret_t>) {
                        std::invoke(fn);
                        wrapper->notify_value();
                    } else {
                        wrapper->notify_value(std::invoke(fn));
                    }
                }
            );
            m_queued_tasks.push(task);
            return await;
        }

        template <typename Fn>
            requires (std::is_nothrow_invocable_r_v<void, Fn>)
        auto queue_work(
            Fn&& fn,
            Task::priority_t p = Task::priority_t::normal
        ) -> void {
            auto task = m_alloc->alloc<queue_item_t>();
            new(task) queue_item_t(
                [fn = std::forward<Fn>(fn), p] noexcept {
                    [[maybe_unused]] auto is_priority_set = ThisThread::set_priority(p);
                    assert(is_priority_set == true);
                    std::invoke(fn);
                }
            );
            m_queued_tasks.push(task);
        }

        auto empty() const noexcept -> bool {
            auto sz = m_trees.size();
            for (auto i = 0ul; i < sz; ++i) {
                auto& t = m_trees[i];
                if (!t.empty()) return false;
            }
            return true;
        }

        auto reset() {
            m_trees.clear();
            m_info.clear();
            m_store.clear();
        }

        auto run() -> std::expected<void, SchedulerError> {
            m_last_processed_task.store(int_to_tid(std::numeric_limits<std::size_t>::max()));
            auto res = build();
            if (!res) return res;
            if (m_tasks == 0) return {};
            m_is_running = true;

            m_pool.waiter.notify_all();
            m_waiter.wait([this] {
                return m_tasks == 0 && m_pool.is_running() && m_queued_tasks.empty();
            });
            m_is_running = false;
            #ifdef __cpp_exceptions
            auto sz = m_info.size();
            for (auto i = 0ul; i < sz; ++i) {
                auto& t = m_info[i];
                if (t.expception_ptr) {
                    std::rethrow_exception(t.expception_ptr);
                }
            }
            #endif
            return {};
        }

        template <typename T>
        auto get_result(TaskId id) -> std::expected<T, ValueStoreError> {
            if (m_is_running) return std::unexpected(ValueStoreError::not_found);
            auto temp = m_store.consume<T>(id);
            return temp.transform([](auto& v) { return v.take(); });
        }

        template <typename T>
        auto get_result(DependencyTracker t) -> std::expected<T, ValueStoreError> {
            return get_result<T>(t.id);
        }

        template <typename T>
        auto get_last_result() -> std::expected<T, ValueStoreError> {
            return get_result<T>(m_last_processed_task.load());
        }
    private:
        auto detect_cycle(TaskId start) const -> bool {
            std::unordered_set<std::size_t> tracker{};
            tracker.insert(tid_to_int(start));
            return detect_cycle(tid_to_int(start), tracker);
        }

        auto detect_cycle(std::size_t start, std::unordered_set<std::size_t>& tracker) const -> bool {
            if (start >= m_info.size()) return false;
            auto& info = m_info[start];
            if (info.state != TaskState::alive) return false;
            for (auto next_id: info.dep_signals) {
                auto nid = tid_to_int(next_id);
                if (tracker.contains(nid)) return true;
                tracker.insert(nid);
                if (detect_cycle(next_id)) return true;
            }
            return false;
        }

        void ensure_space_for(std::size_t size) {
            m_trees.resize((size + capacity - 1) / capacity);
            m_info.resize(size);
            m_store.resize(size);
        }

        auto pop_task() -> std::optional<TaskId> {
            auto sz = m_trees.size();
            for (auto i = 0ul; i < sz; ++i) {
                auto& t = m_trees[i];
                auto [idx, _] = t.select();
                if (idx.is_invalid()) continue;
                return { int_to_tid(idx.index) };
            }
            return {};
        }

        auto complete_one_task() -> void {
            m_waiter.notify_all([this] {
                m_tasks.fetch_sub(1);
            });
        }
    private:
        std::unique_ptr<BlockAllocator> m_alloc{ std::make_unique<BlockAllocator>() };
        BlockSizedList<signal_tree, 1> m_trees;
        BlockSizedList<TaskInfo, capacity> m_info;
        std::atomic<std::size_t> m_tasks{0};
        std::atomic<bool> m_is_running{false};
        ValueStore m_store{m_alloc.get()};
        WorkerPool m_pool;
        internal::Waiter m_waiter;
        std::atomic<TaskId> m_last_processed_task{int_to_tid(std::numeric_limits<std::size_t>::max())};
        Queue<queue_item_t*> m_queued_tasks;
    };

    inline auto TaskToken::schedule() noexcept -> void {
        if (m_id == invalid_task_id) return;
        auto id = tid_to_int(m_id);
        auto& info = m_parent.m_info[id];
        if (info.state != Scheduler::TaskState::alive) return;
        m_parent.set_signal(m_id);
        m_result = TaskResult::rescheduled;
    }

    inline auto TaskToken::stop() noexcept -> void {
        if (m_id == invalid_task_id) return;
        m_store.remove(m_id);
        auto id = tid_to_int(m_id);
        m_parent.m_info[id].state.store(Scheduler::TaskState::empty);
        m_result = TaskResult::failed;
    }

    template <typename Fn>
        requires (std::is_nothrow_invocable_r_v<void, Fn>)
    inline auto TaskToken::queue_work(
        Fn&& fn,
        ThisThread::Priority p
    ) -> void {
        m_parent.queue_work(std::forward<Fn>(fn), p);
    }

    template <typename Fn>
        requires (std::is_nothrow_invocable_v<Fn>)
    inline auto TaskToken::awaitable_queue_work(
        Fn&& fn,
        ThisThread::Priority p
    ) -> Awaiter<decltype(std::invoke(fn))> {
        return m_parent.awaitable_queue_work(std::forward<Fn>(fn), p);
    }


    inline auto Scheduler::DependencyTracker::deps_on(
        std::span<DependencyTracker> ids
    ) -> std::expected<void, SchedulerError> {
        for ([[maybe_unused]] auto [child, p]: ids) {
            assert(p == parent);
            if (id == child) {
                return std::unexpected(SchedulerError::cycle_found);
            }

            auto cid = tid_to_int(child);

            if (parent->m_info.size() <= cid) continue;
            auto& tmp = parent->m_info[cid];
            if (tmp.state != TaskState::alive) continue;
            auto& ds = tmp.dep_signals;
            if (std::find(ds.begin(), ds.end(), id) == ds.end()) {
                ds.push_back(id);
                if (parent->detect_cycle(child)) {
                    ds.pop_back();
                    return std::unexpected(SchedulerError::cycle_found);
                }
                auto& info = parent->m_info[tid_to_int(id)];
                info.signals.fetch_add(1);
                info.inputs.push_back({ child, false });
            }
        }
        return {};
    }

    inline auto WorkerPool::do_work(std::size_t thread_id) -> void {
        ThisThread::s_pool_id = thread_id;

        while (m_is_running.load(std::memory_order_acquire)) {
            waiter.wait([this] {
                return !m_is_running.load(std::memory_order_acquire) || (
                        m_parent.m_is_running.load(std::memory_order_acquire) &&
                        (
                            (m_parent.m_tasks.load(std::memory_order_acquire) != 0) ||
                            !m_parent.m_queued_tasks.empty()
                        )
                );
            });

            auto idx = m_parent.pop_task();
            if (!idx.has_value()) {
                auto work = m_parent.m_queued_tasks.pop();
                if (work) {
                    auto* w = *work;
                    auto& task = *w;
                    auto token = TaskToken(
                        m_parent,
                        m_parent.m_store
                    );
                    task();
                    m_parent.m_alloc->dealloc(w);
                }
                continue;
            }
            auto id = idx.value();
            auto& info = m_parent.m_info[tid_to_int(id)];
            auto token = TaskToken(
                m_parent,
                id,
                m_parent.m_store,
                info.inputs
            );
            #ifdef __cpp_exceptions
            try {
                info.task(token);
            } catch (std::exception const& e) {
                if (!info.error_handler) {
                    info.expception_ptr = std::current_exception();
                    token.m_result = TaskResult::failed;
                } else {
                    auto should_continue = info.error_handler(e); 
                    if (!should_continue) {
                        token.m_result = TaskResult::failed;
                    } else if (token.m_result == TaskResult::success) {
                        token.m_result = TaskResult::failed;
                    }
                }
            }
            #else
                info.task(token);
            #endif
            if (id == invalid_task_id) return;
            switch (token.m_result) {
            case TaskResult::success: m_parent.on_complete(id, true); break;
            case TaskResult::failed: m_parent.on_failure(id); break;
            case TaskResult::rescheduled: m_parent.on_reschedule(id); break;
            }
        }

        ThisThread::s_pool_id = std::numeric_limits<std::size_t>::max();
    }

} // namespace tpl

#endif // AMT_TPL_SCHEDULAR_HPP
