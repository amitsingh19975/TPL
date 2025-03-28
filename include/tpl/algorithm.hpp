#ifndef AMT_TPL_ALGORITHM_HPP
#define AMT_TPL_ALGORITHM_HPP

#include "scheduler.hpp"
#include "range.hpp"
#include "task_token.hpp"
#include <type_traits>

namespace tpl::par {

    namespace internal {
        template <std::size_t Chunks = 512, typename Fn, bool R>
        auto for_each(
            Scheduler& s,
            Range<R> r,
            Fn&& fn,
            auto&& dep_fn
        ) -> std::expected<void, SchedularError> {
            auto items = (r.size() + Chunks - 1) / Chunks;
            for (auto i = 0ul; i < items; ++i) {
                auto start = r.start + i * Chunks * r.stride;
                auto end = std::min(start + r.stride * Chunks, r.end);
                auto range = Range<R>(start, end, r.stride);
                auto t = s.add_task([range, &fn](auto& t) {
                    if constexpr (std::is_invocable_v<Fn, Range<R>, TaskToken&>) {
                        return std::invoke(fn, range, t);
                    } else if constexpr (std::is_invocable_v<Fn, TaskToken&, Range<R>>) {
                        return std::invoke(fn, t, range);
                    } else if constexpr (std::is_invocable_v<Fn, Range<R>>) {
                        return std::invoke(fn, range);
                    } else if constexpr (std::is_invocable_v<Fn, TaskToken&>) {
                        return std::invoke(fn, t);
                    } else {
                        return std::invoke(fn);
                    }
                });
                using ret_t = decltype(dep_fn(t));
                if constexpr (!std::is_void_v<ret_t>) {
                    auto res = dep_fn(t);
                    if (!res) return res;
                } else {
                    dep_fn(t);
                }
            }
            return {};
        }
    } // namespace internal

    template <std::size_t Chunks = 512, typename Fn, bool R>
    auto for_each(Scheduler& s, Range<R> r, Fn&& fn) {
        (void) internal::for_each<Chunks>(
            s,
            r,
            std::forward<Fn>(fn),
            [](auto){}
        );
    }

    template <std::size_t Chunks = 512, typename Fn, bool R>
    auto for_each(
        Scheduler& s,
        Range<R> r,
        Scheduler::DependencyTracker d,
        Fn&& fn
    ) -> std::expected<void, SchedularError> {
        return internal::for_each<Chunks>(
            s,
            r,
            std::forward<Fn>(fn),
            [d](auto t) {
                return t.deps_on(d);
            }
        );
    }
} // namespace tpl::par

#endif // AMT_TPL_ALGORITHM_HPP
