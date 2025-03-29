#ifndef AMT_TPL_TASK_TOKEN_HPP
#define AMT_TPL_TASK_TOKEN_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>
#include <utility>
#include "worker_pool.hpp"
#include "value_store.hpp"
#include "task_id.hpp"
#include "cow.hpp"

namespace tpl {
    enum class TaskError {
        type_mismatch,
        not_found,
        invalid_task_id,
        arity_mismatch
    };

    constexpr auto to_task_error(ValueStoreError e) noexcept -> TaskError {
        switch (e) {
        case ValueStoreError::type_mismatch: return TaskError::type_mismatch;
        case ValueStoreError::not_found: return TaskError::not_found;
        }
    }

    constexpr auto to_string(TaskError e) noexcept -> std::string_view {
        switch (e) {
            case TaskError::type_mismatch: return "Type Mismatch";
            case TaskError::not_found: return "Not Found";
            case TaskError::invalid_task_id: return "Invalid Task";
            case TaskError::arity_mismatch: return "Arity Mismatch";
        }
    }

    struct Scheduler;
    enum class TaskResult: std::uint8_t {
        success,
        failed,
        rescheduled
    };

    struct TaskToken {
        static constexpr auto invalid = std::numeric_limits<std::size_t>::max();
        constexpr TaskToken(TaskToken const&) noexcept = delete;
        constexpr TaskToken(TaskToken &&) noexcept = delete;
        constexpr TaskToken& operator=(TaskToken const&) noexcept = delete;
        constexpr TaskToken& operator=(TaskToken &&) noexcept = delete;
        constexpr ~TaskToken() noexcept = default;

        constexpr TaskToken(
            Scheduler& parent,
            TaskId tid,
            ValueStore& store,
            std::vector<std::pair<TaskId, bool /*consumable*/>> inputs
        )
            : m_id(tid)
            , m_store(store)
            , m_inputs(std::move(inputs))
            , m_parent(parent)
        {}

        constexpr auto owner_id() const noexcept -> TaskId {
            return m_id;
        }

        template <typename T>
        auto return_(T&& val) -> bool {
            if (m_result == TaskResult::failed) return false;
            m_store.put(m_id, std::forward<T>(val));
            return true;
        }

        template <typename T>
        [[nodiscard]] auto arg(TaskId id) -> std::expected<Cow<T>, TaskError> {
            auto it = std::find_if(m_inputs.begin(), m_inputs.end(), [id](auto el) {
                return el.first == id;
            });

            if (it == m_inputs.end()) {
                return std::unexpected(TaskError::invalid_task_id);
            }

            auto consumable = it->second;

            if (consumable) {
                auto tmp = m_store.consume<T>(id).transform_error([](ValueStoreError e) {
                    return to_task_error(e);
                });
                return std::move(tmp);
            } else {
                auto tmp = m_store.get<T>(id);
                if (tmp) return std::move(tmp.value());
                return std::unexpected(to_task_error(tmp.error()));
            }
        }

        template <typename T>
        [[nodiscard]] auto all_of() -> std::vector<Cow<T>> {
            std::vector<Cow<T>> res;
            res.reserve(m_inputs.size());
            for (auto [id, _]: m_inputs) {
                auto tmp = arg<T>(id);
                if (!tmp) continue;
                res.emplace_back(std::move(*tmp));
            }
            return res;
        }

        template <typename... Ts>
            requires (sizeof...(Ts) > 0)
        [[nodiscard]] auto arg() -> std::tuple<std::expected<Cow<Ts>, TaskError>...> {
            std::array type_ids{ internal::ValueStoreDestructor<Ts>::destroy... };
            std::array<std::size_t, sizeof...(Ts)> ids;
            std::fill(ids.begin(), ids.end(), invalid);

            for (auto id: m_inputs) {
                auto tid = m_store.get_type(id.first);
                for (auto i = 0ul; i < type_ids.size(); ++i) {
                    if (tid == type_ids[i] && ids[i] == invalid) {
                        ids[i] = tid_to_int(id.first);
                        break;
                    }
                }
            }

            auto helper = [this, &ids]<std::size_t... Is>(std::index_sequence<Is...>)
                -> std::tuple<std::expected<Cow<Ts>, TaskError>...>
            {
                return std::make_tuple(std::move(this->arg<Ts>(int_to_tid(ids[Is])))...);
            };
            return helper(std::make_index_sequence<sizeof...(Ts)>{});
        }

        auto schedule() noexcept -> void;
        auto stop() noexcept -> void;

        constexpr auto is_success() const noexcept -> bool {
            return m_result == TaskResult::success;
        }
    private:
        friend struct WorkerPool;
    private:
        TaskId m_id{};
        ValueStore& m_store;
        std::vector<std::pair<TaskId, bool /*consumable*/>> m_inputs;
        TaskResult m_result{ TaskResult::success };
        Scheduler& m_parent;
    };
} // namespace tpl

#endif // AMT_TPL_TASK_TOKEN_HPP
