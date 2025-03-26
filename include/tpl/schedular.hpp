#ifndef AMT_TPL_SCHEDULAR_HPP
#define AMT_TPL_SCHEDULAR_HPP

#include "signal_tree/tree.hpp"
#include "task.hpp"
#include "task_token.hpp"
#include "atomic.hpp"
#include "value_store.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>
#include <span>

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
        using task_id = std::size_t;
        static constexpr std::size_t capacity = 128ul;
        using signal_tree = SignalTree<capacity>;
    private:
        friend struct TaskToken;
        enum class TaskState: std::uint8_t {
            empty = 0,
            alive = 1,
            dead  = 2
        };
        struct TaskInfo {
            Task task;
            // INFO: Dependencies to signal when this completes
            std::vector<task_id> dep_signals{};

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

        auto set_signal(task_id id) -> void {
            if (m_info[id].state != TaskState::alive) return;
            auto [b, p] = parse_task_id(id);
            m_trees[b].set(p);
        }

        constexpr auto parse_task_id(
            task_id id
        ) const noexcept -> std::pair<std::size_t, std::size_t> {
            return { id / capacity, id % capacity };
        }

        auto on_complete(task_id id) {
            auto& info = m_info[id];
            if (info.state != TaskState::alive) return;
            for (auto i: info.dep_signals) {
                auto& task = m_info[i];
                if (task.state != TaskState::alive) continue;
                if (task.signals.fetch_sub(1) == 0) {
                    auto [b, p] = parse_task_id(i);
                    m_trees[b].set(p);
                }
            }
        }

        struct DependencyTracker {
            task_id id;
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
    public:

        template <typename Fn>
        constexpr auto add_task(
            Fn&& fn,
            Task::priority_t p = Task::priority_t::normal
        ) -> DependencyTracker {
            for (auto i = m_trees.size(); i > 0; --i) {
                auto idx = i - 1;
                auto& block = m_trees[idx];
                auto pos = block.get_empty_pos();
                if (!pos) continue;
                block.set(*pos);
                auto index = idx * capacity + *pos;
                if (m_info.size() <= index) resize_info(m_trees.size() * capacity + 1);
                m_info[index] = TaskInfo(
                    Task(std::forward<Fn>(fn), p)
                );
                return { .id = index, .parent = this };
            }
            auto block = signal_tree{};
            block.set(0);
            m_trees.emplace_back(std::move(block));
            auto index = m_trees.size() - 1;
            if (m_info.size() <= index) resize_info(m_trees.size() * capacity + 1);
            m_info[index] = TaskInfo(
                Task(std::forward<Fn>(fn), p)
            );
            return { .id = index, .parent = this };
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
                for (auto dep: info.dep_signals) in_edges[dep]++;
            }

            // 2. queue tasks if there is no deps
            for (auto i = 0ul; i < in_edges.size(); ++i) {
                auto& info = m_info[i];
                if (info.state != TaskState::alive) continue;
                /*std::println("Edges[{}]: {}", i, in_edges[i]);*/
                if (in_edges[i] == 0) {
                    set_signal(i);
                }
            }

            if (empty()) {
                return std::unexpected(SchedularError::no_root_task);
            }
            /*m_trees[0].debug_print();*/
            /*for (auto i = 0ul; auto const& info: m_info) {*/
            /*    ++i;*/
            /*    if (info.state != TaskState::alive) continue;*/
            /*    std::println("Task[{}]: {} | {}", i - 1, info.produces_immovable_value(), info.dep_signals);*/
            /*}*/
            return {};
        }

        auto empty() const noexcept -> bool {
            for(auto const& t: m_trees) {
                if (!t.empty()) return false;
            }
            return true;
        }

        auto reset() {
            for (auto& t: m_trees) t.clear();
            for (auto& info: m_info) info.state.store(TaskState::empty);
            m_store.clear();
        }
    private:
        auto detect_cycle(task_id start) const -> bool {
            std::unordered_set<task_id> tracker{};
            return detect_cycle(start, tracker);
        }

        auto detect_cycle(task_id start, std::unordered_set<task_id>& tracker) const -> bool {
            if (start >= m_info.size()) return false;
            auto& info = m_info[start];
            if (info.state != TaskState::alive) return false;
            for (auto next_id: info.dep_signals) {
                if (tracker.contains(start)) return true;
                tracker.insert(start);
                if (detect_cycle(next_id, tracker)) return true;
            }
            return false;
        }

        void resize_info(std::size_t size) {
            m_info.resize(size);
            m_store.resize(size);
        }
    private:
        std::unique_ptr<BlockAllocator> m_alloc;
        std::vector<signal_tree> m_trees{1};
        std::vector<TaskInfo> m_info{capacity};
        ValueStore m_store{m_info.size(), m_alloc.get()};
    };

    inline auto TaskToken::schedule() noexcept -> void {
        m_parent.set_signal(m_id);
        m_result = TaskResult::rescheduled;
    }

    inline auto TaskToken::stop() noexcept -> void {
        m_store.remove(m_id);
        m_result = TaskResult::failed;
    }

    inline auto TaskToken::destroy() noexcept -> void {
        m_store.remove(m_id);
        m_parent.m_info[m_id].state.store(Schedular::TaskState::dead);
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

            auto& tmp = parent->m_info[child];
            if (tmp.state != TaskState::alive) continue;
            auto& ds = tmp.dep_signals;
            if (std::find(ds.begin(), ds.end(), id) == ds.end()) {
                ds.push_back(id);
                if (parent->detect_cycle(child)) {
                    ds.pop_back();
                    return std::unexpected(SchedularError::cycle_found);
                }
                parent->m_info[id].signals.fetch_add(1);
            }
        }
        return {};
    }

} // namespace tpl

#endif // AMT_TPL_SCHEDULAR_HPP
