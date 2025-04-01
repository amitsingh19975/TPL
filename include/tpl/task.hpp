#ifndef AMT_TPL_TASK_HPP
#define AMT_TPL_TASK_HPP

#include <concepts>
#include <exception>
#include <functional>
#include <type_traits>
#include <utility>
#include "thread.hpp"
#include "task_token.hpp"

namespace tpl {

    struct Task {
        using priority_t = ThisThread::Priority;
        using fn_t = std::function<void(TaskToken&)>;

        template <typename Fn>
            requires (
                std::invocable<Fn> ||
                std::invocable<Fn, TaskToken&>
            )
        explicit Task(Fn&& fn, priority_t p = priority_t::normal) noexcept
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

        auto operator()(TaskToken& token) const {
            [[maybe_unused]] auto is_priority_set = ThisThread::set_priority(m_priority);
            assert(is_priority_set == true);
            m_fn(token);
        }

        constexpr auto priority() const noexcept -> priority_t {
            return m_priority;
        }
    private:
        fn_t m_fn;
        priority_t m_priority{ priority_t::normal };
    };


    struct ErrorHandler {
        ErrorHandler() noexcept = default;
        ErrorHandler(ErrorHandler const&) noexcept = default;
        ErrorHandler(ErrorHandler &&) noexcept = default;
        ErrorHandler& operator=(ErrorHandler const&) noexcept = default;
        ErrorHandler& operator=(ErrorHandler &&) noexcept = default;
        ~ErrorHandler() noexcept = default;

        constexpr operator bool() const noexcept {
            return bool(m_handler);
        }

        template <typename Fn>
            requires (
                std::invocable<Fn, std::exception const&> ||
                std::invocable<Fn, std::exception const&> || 
                std::invocable<Fn> || 
                std::invocable<Fn>
            )
        explicit ErrorHandler(Fn&& fn) {
            m_handler = [fn = std::forward<Fn>(fn)](std::exception const& e) noexcept -> bool {
                if constexpr (std::invocable<Fn, std::exception const&>) {
                    using ret_t = decltype(std::invoke(fn, e));
                    if constexpr (std::is_void_v<ret_t>) {
                        std::invoke(fn, e);
                        return false;
                    } else {
                        return std::invoke(fn, e);
                    }
                } else {
                    using ret_t = decltype(std::invoke(fn));
                    if constexpr (std::is_void_v<ret_t>) {
                        std::invoke(fn);
                        return false;
                    } else {
                        return std::invoke(fn);
                    }
                }
            };
        }

        auto operator()(std::exception const& e) const noexcept -> bool {
            if (!m_handler) return false;
            return m_handler(std::move(e));
        }
    private:
        std::function<bool(std::exception const&)> m_handler{nullptr};
    };
} // namespace tpl

#endif // AMT_TPL_TASK_HPP
