#ifndef AMT_TPL_SCHEDULAR_HPP
#define AMT_TPL_SCHEDULAR_HPP

#include "signal_tree/tree.hpp"
#include "task.hpp"
#include "waiter.hpp"
#include "worker_pool.hpp"
#include "task_token.hpp"
#include "atomic.hpp"
#include "value_store.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>
#include <span>
#include "task_id.hpp"

namespace tpl {

    enum class SchedularError {
        no_root_task,
        cycle_found
    };

    constexpr auto to_string(SchedularError e) noexcept -> std::string_view {
        switch (e) {
            case SchedularError::no_root_task: return "There must be a root task that does not depends on any other tasks.";
            case SchedularError::cycle_found: return "Cycle detected";
        }
    }

    struct Schedular {
        static constexpr std::size_t capacity = 128ul;
        using signal_tree = SignalTree<capacity>;

        Schedular()
            : m_pool(*this)
        {}
        Schedular(Schedular const&) = delete;
        Schedular(Schedular &&) = delete;
        Schedular& operator=(Schedular const&) = delete;
        Schedular& operator=(Schedular &&) = delete;
        ~Schedular() = default;
    private:
        friend struct TaskToken;
        friend struct WorkerPool;

        enum class TaskState: std::uint8_t {
            empty = 0,
            alive = 1,
            dead  = 2
        };
        struct TaskInfo {
            Task task;
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

            TaskInfo(Task t) noexcept
                : task(std::move(t))
                , state(TaskState::alive)
            {}

            TaskInfo() noexcept = default;
            TaskInfo(TaskInfo const&) = delete;
            TaskInfo(TaskInfo && other) noexcept
                : task(std::move(other.task))
                , dep_signals(std::move(other.dep_signals))
                , signals(other.signals.load())
                , state(other.state.load())
            {}
            TaskInfo& operator=(TaskInfo const&) = delete;
            TaskInfo& operator=(TaskInfo && other) noexcept {
                if (this == &other) return *this;
                using std::swap;
                swap(task, other.task);
                swap(dep_signals, other.dep_signals);
                signals.store(other.signals.load());
                state.store(other.state.load());
                return *this;
            }
            ~TaskInfo() = default;
        };

        auto set_signal(TaskId id) -> void {
            if (m_info[tid_to_int(id)].state != TaskState::alive) return;
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
        }

        auto on_failure(TaskId id) {
            (void)id;
            complete_one_task();
        }

        auto on_reschedule(TaskId id) {
            (void)id;
            m_pool.waiter.notify_one();
        }

        auto build() -> std::expected<void, SchedularError> {
            for (auto& t: m_trees) {
                t.clear();
            }
            // 1. find incoming edges
            // Now we need to find nodes that do not have incoming edges
            std::vector<std::size_t> in_edges(m_info.size(), 0);
            for (auto const& info: m_info) {
                // Need to consider both dead and alive
                if (info.state == TaskState::empty) continue;
                for (auto dep: info.dep_signals) in_edges[tid_to_int(dep)]++;
            }

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
            for (auto i = 0ul; i < m_info.size(); ++i) {
                auto& info = m_info[i];
                if (info.state != TaskState::alive) continue;
                freq[i] += info.dep_signals.size();
            }

            for (auto i = 0ul; i < m_info.size(); ++i) {
                auto& info = m_info[i];
                if (info.state != TaskState::alive) continue;
                for (auto j = 0ul; j < info.inputs.size(); ++j) {
                    auto& in = info.inputs[j];
                    in.second = (freq[tid_to_int(in.first)] > 0);
                }
            }

            if (empty()) {
                return std::unexpected(SchedularError::no_root_task);
            }
            return {};
        }
    public:

        struct DependencyTracker {
            TaskId id;
            Schedular* parent;

            auto deps_on(
                std::span<DependencyTracker> ids
            ) -> std::expected<void, SchedularError>;

            template <typename... Ts>
                requires ((std::same_as<Ts, DependencyTracker> && ...) && (sizeof...(Ts) > 0))
            auto deps_on(Ts... ids) -> std::expected<void, SchedularError> {
                std::array tmp { ids... };
                return deps_on(tmp);
            }
        };

        template <typename Fn>
        constexpr auto add_task(
            Fn&& fn,
            Task::priority_t p = Task::priority_t::normal
        ) -> DependencyTracker {
            if (m_trees.size() * capacity > m_info.size()) resize_info(m_trees.size() * capacity);

            for (auto i = 0ul; i < m_info.size(); ++i) {
                auto& info = m_info[i];
                if (info.state == TaskState::alive) continue;
                m_info[i] = TaskInfo(Task(std::forward<Fn>(fn), p));
                return { .id = int_to_tid(i), .parent = this };
            }

            m_trees.push_back(signal_tree{});
            auto size = m_info.size();
            resize_info(m_trees.size() * capacity);
            m_info[size] = TaskInfo(Task(std::forward<Fn>(fn), p));
            return { .id = int_to_tid(size), .parent = this };
        }

        auto empty() const noexcept -> bool {
            for(auto const& t: m_trees) {
                if (!t.empty()) return false;
            }
            return true;
        }

        auto reset() {
            for (auto& t: m_trees) t.clear();
            m_info.clear();
            m_store.clear();
        }

        auto run() -> std::expected<void, SchedularError> {
            auto res = build();
            if (!res) return res;
            if (m_tasks == 0) return {};
            m_is_running = true;

            m_pool.waiter.notify_all();
            waiter.wait([this] {
                return m_tasks == 0 && m_pool.is_running();
            });
            m_is_running = false;
            return {};
        }
    private:
        auto detect_cycle(TaskId start) const -> bool {
            std::unordered_set<std::size_t> tracker{};
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

        void resize_info(std::size_t size) {
            m_info.resize(size);
            m_store.resize(size);
        }

        auto pop_task() -> std::optional<TaskId> {
            for (auto& t: m_trees) {
                auto [idx, _] = t.select();
                if (idx.is_invalid()) continue;
                return { int_to_tid(idx.index) };
            }
            return {};
        }

        auto complete_one_task() -> void {
            waiter.notify_all([this] {
                m_tasks.fetch_sub(1);
            });
        }
    private:
        std::unique_ptr<BlockAllocator> m_alloc{ std::make_unique<BlockAllocator>() };
        std::vector<signal_tree> m_trees{1};
        std::vector<TaskInfo> m_info{capacity};
        std::atomic<std::size_t> m_tasks{false};
        std::atomic<bool> m_is_running{false};
        ValueStore m_store{m_info.size(), m_alloc.get()};
        WorkerPool m_pool;
        internal::Waiter waiter;
    };

    inline auto TaskToken::schedule() noexcept -> void {
        auto id = tid_to_int(m_id);
        auto& info = m_parent.m_info[id];
        if (info.state != Schedular::TaskState::alive) return;
        m_parent.set_signal(m_id);
        m_result = TaskResult::rescheduled;
    }

    inline auto TaskToken::stop() noexcept -> void {
        m_store.remove(m_id);
        auto id = tid_to_int(m_id);
        m_parent.m_info[id].state.store(Schedular::TaskState::empty);
        m_result = TaskResult::failed;
    }

    inline auto Schedular::DependencyTracker::deps_on(
        std::span<DependencyTracker> ids
    ) -> std::expected<void, SchedularError> {
        for ([[maybe_unused]] auto [child, p]: ids) {
            assert(p == parent);
            if (id == child) {
                return std::unexpected(SchedularError::cycle_found);
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
                    return std::unexpected(SchedularError::cycle_found);
                }
                auto& info = parent->m_info[tid_to_int(id)];
                info.signals.fetch_add(1);
                info.inputs.push_back({ child, false });
            }
        }
        return {};
    }

    inline auto WorkerPool::do_work() -> void {
        while (m_is_running.load(std::memory_order_acquire)) {
            waiter.wait([this] {
                return !m_is_running.load(std::memory_order_acquire) || (
                       m_parent.m_is_running.load(std::memory_order_acquire) &&
                       m_parent.m_tasks.load(std::memory_order_acquire) != 0);
            });

            auto idx = m_parent.pop_task();
            if (!idx.has_value()) continue;
            auto id = idx.value();
            auto& info = m_parent.m_info[tid_to_int(id)];
            auto token = TaskToken(
                m_parent,
                id,
                m_parent.m_store,
                info.inputs
            );
            info.task(token);
            switch (token.m_result) {
            case TaskResult::success: m_parent.on_complete(id, true); break;
            case TaskResult::failed: m_parent.on_failure(id); break;
            case TaskResult::rescheduled: m_parent.on_reschedule(id); break;
            }
        }
    }

} // namespace tpl

#endif // AMT_TPL_SCHEDULAR_HPP
