#ifndef AMT_TPL_TASK_HPP
#define AMT_TPL_TASK_HPP

#include <concepts>
#include <functional>
#include <type_traits>
#include "thread.hpp"
#include "task_token.hpp"

namespace tpl {

    struct Task {
        using priority_t = ThisThread::Priority;
        using fn_t = std::function<void(TaskToken&)>;

        template <typename Fn>
        Task(Fn&& fn, priority_t p = priority_t::normal) noexcept
            : m_priority(p)
        {
            m_fn = [fn = std::forward<Fn>(fn)](TaskToken& t) {
                if constexpr (std::invocable<Fn, TaskToken&>) {
                    using ret_t = decltype(std::invoke(fn, t));
                    if constexpr (!std::is_void_v<ret_t>) {
                        decltype(auto) res = std::invoke(fn, t);
                        if (t.is_success()) {
                            t.return_(std::forward<decltype(res)>(res));
                        }
                    } else {
                        (void)std::invoke(fn, t);
                    }
                } else {
                    using ret_t = decltype(std::invoke(fn));
                    if constexpr (!std::is_void_v<ret_t>) {
                        decltype(auto) res = std::invoke(fn);
                        if (t.is_success()) {
                            t.return_(std::forward<decltype(res)>(res));
                        }
                    } else {
                        (void)std::invoke(fn);
                    }
                }
            };
        }

        Task() noexcept = default;
        Task(Task const&) noexcept = delete;
        Task(Task &&) noexcept = default;
        Task& operator=(Task const&) noexcept = delete;
        Task& operator=(Task &&) noexcept = default;
        ~Task() noexcept = default;

        auto operator()(TaskToken& token) const noexcept {
            [[maybe_unused]] auto is_priority_set = ThisThread::set_priority(m_priority);
            assert(is_priority_set == true);
            m_fn(token);
        }
    private:
        fn_t m_fn;
        priority_t m_priority{ priority_t::normal };
    };

} // namespace tpl

#endif // AMT_TPL_TASK_HPP
