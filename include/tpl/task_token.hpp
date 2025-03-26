#ifndef AMT_TPL_TASK_TOKEN_HPP
#define AMT_TPL_TASK_TOKEN_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>
#include <utility>
#include "value_store.hpp"

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

    struct Schedular; 
    enum class TaskResult: std::uint8_t {
        success,
        failed,
        rescheduled
    };

    struct TaskToken {
        using task_id = std::size_t;
        static constexpr auto invalid = std::numeric_limits<task_id>::max();
        constexpr TaskToken(TaskToken const&) noexcept = delete;
        constexpr TaskToken(TaskToken &&) noexcept = delete;
        constexpr TaskToken& operator=(TaskToken const&) noexcept = delete;
        constexpr TaskToken& operator=(TaskToken &&) noexcept = delete;
        constexpr ~TaskToken() noexcept = default;


        constexpr TaskToken(
            Schedular& parent,
            task_id tid,
            ValueStore& store,
            std::vector<task_id> inputs,
            bool consumes = true
        )
            : m_id(tid)
            , m_store(store)
            , m_inputs(std::move(inputs))
            , m_consumes(consumes)
            , m_parent(parent)
        {}

        template <typename T>
        auto return_(T&& val) {
            m_store.put(m_id, std::forward<T>(val));
        }

        template <typename T>
        [[nodiscard]] auto arg(task_id id) -> std::expected<T, TaskError> {
            if (std::find(m_inputs.begin(), m_inputs.end(), id) == m_inputs.end()) {
                return std::unexpected(TaskError::invalid_task_id);
            }

            if (m_consumes) {
                auto tmp = m_store.consume<T>(id).transform_error([](ValueStoreError e) {
                    return to_task_error(e);
                });
                return std::move(tmp);
            } else {
                auto tmp = m_store.get<T>(id);
                if (tmp) return { tmp.value().get() };
                return std::unexpected(to_task_error(tmp.error()));
            }
        }

        template <typename... Ts>
            requires (sizeof...(Ts) > 0)
        [[nodiscard]] auto arg() -> std::tuple<std::expected<Ts, TaskError>...> {
            std::array type_ids{ internal::ValueStoreDestructor<Ts>::destroy... };
            std::array<task_id, sizeof...(Ts)> ids;
            static constexpr auto npos = std::numeric_limits<task_id>::max();
            std::fill(ids.begin(), ids.end(), npos);

            for (auto id: m_inputs) {
                auto tid = m_store.get_type(id);
                for (auto i = 0ul; i < type_ids.size(); ++i) {
                    if (tid == type_ids[i] && ids[i] == npos) {
                        ids[i] = id;
                        break;
                    }
                }
            }

            auto helper = [this, &ids]<std::size_t... Is>(std::index_sequence<Is...>)
                -> std::tuple<std::expected<Ts, TaskError>...>
            {
                return std::make_tuple(std::move(this->arg<Ts>(ids[Is]))...);
            };
            return helper(std::make_index_sequence<sizeof...(Ts)>{});
        }

        constexpr auto is_rescheduled() const noexcept { return m_result == TaskResult::rescheduled; }
        constexpr auto get_result() const noexcept { return m_result; }

        auto stop() noexcept -> void;
        auto schedule() noexcept -> void;
        auto destroy() noexcept -> void;
    private:
        task_id m_id{};
        ValueStore& m_store;
        std::vector<task_id> m_inputs;
        bool m_consumes{true};
        TaskResult m_result{ TaskResult::success };
        Schedular& m_parent;
    };
} // namespace tpl

#endif // AMT_TPL_TASK_TOKEN_HPP
