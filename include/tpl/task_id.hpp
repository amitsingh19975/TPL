#ifndef AMT_TPL_TASK_ID_HPP
#define AMT_TPL_TASK_ID_HPP

#include <cstddef>
#include <limits>

namespace tpl {
    enum class TaskId: std::size_t {};
    static constexpr auto invalid_task_id = static_cast<TaskId>(
        std::numeric_limits<std::size_t>::max()
    );

    constexpr auto tid_to_int(TaskId id) noexcept -> std::size_t {
        return static_cast<std::size_t>(id);
    }

    constexpr auto int_to_tid(std::size_t id) noexcept -> TaskId {
        return static_cast<TaskId>(id);
    }
} // namespace tpl

#endif // AMT_TPL_TASK_ID_HPP
