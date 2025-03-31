#ifndef AMT_TPL_ALGORITHM_HPP
#define AMT_TPL_ALGORITHM_HPP

#include "scheduler.hpp"
#include "range.hpp"
#include "task_token.hpp"
#include <cstddef>
#include <iterator>
#include <numeric>
#include <type_traits>

namespace tpl::par {

    namespace internal {
        template <std::size_t Chunks = 512, typename Fn, bool R>
        auto for_each(
            Scheduler& s,
            Range<R> r,
            Fn&& fn,
            auto&& dep_fn
        ) -> std::expected<void, SchedulerError> {
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

        template <std::size_t Chunks = 512, typename Acc, typename I, typename Fn>
            requires (std::incrementable<I>)
        auto reduce(
            Scheduler& s,
            I b,
            I e,
            Acc acc,
            Fn&& fn,
            auto&& dep_fn
        ) -> std::expected<Scheduler::DependencyTracker, SchedulerError> {
            auto size = static_cast<std::size_t>(std::distance(b, e));
            auto items = (size + Chunks - 1) / Chunks;

            auto reduce_task = s.add_task([acc, &fn](TaskToken& t) {
                auto args = t.all_of<Acc>();
                return std::accumulate(args.begin(), args.end(), acc, [&fn](Acc acc, auto& v) {
                    if constexpr (std::is_invocable_v<Fn, Acc, Acc, TaskToken*>) {
                        return std::invoke(fn, acc, v.ref(), nullptr);
                    } else if constexpr (std::is_invocable_v<Fn, TaskToken*, Acc, Acc>) {
                        return std::invoke(fn, nullptr, acc, v.ref());
                    } else {
                        return std::invoke(fn, acc, v.ref());
                    }
                });
            });

            for (auto i = 0ul; i < items; ++i) {
                auto start = i * Chunks;
                auto end = std::min(start + Chunks, size);
                auto nb = std::next(b, static_cast<std::ptrdiff_t>(start));
                auto ne = std::next(b, static_cast<std::ptrdiff_t>(end));
                auto t = s.add_task([b = nb, e = ne, &fn](TaskToken& t) {
                    auto res = Acc{};
                    for (auto it = b; it != e; ++it) {
                        if constexpr (std::is_invocable_v<Fn, Acc, Acc, TaskToken*>) {
                            res = std::invoke(fn, res, *it, &t);
                        } else if constexpr (std::is_invocable_v<Fn, TaskToken*, Acc, Acc>) {
                            res = std::invoke(fn, &t, res, *it);
                        } else {
                            res = std::invoke(fn, res, *it);
                        }
                    }
                    return res;
                });

                using ret_t = decltype(dep_fn(t));
                if constexpr (!std::is_void_v<ret_t>) {
                    auto res = dep_fn(t);
                    if (!res) return std::unexpected(res.error());
                } else {
                    dep_fn(t);
                }

                reduce_task.deps_on(t);
            }

            return reduce_task;
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
    ) -> std::expected<void, SchedulerError> {
        return internal::for_each<Chunks>(
            s,
            r,
            std::forward<Fn>(fn),
            [d](auto t) {
                return t.deps_on(d);
            }
        );
    }

    template <std::size_t Chunks = 512, typename Acc, typename I, typename Fn>
        requires (std::incrementable<I>)
    auto reduce(
        Scheduler& s,
        I b,
        I e,
        Acc acc,
        Fn&& fn
    ) -> std::expected<Scheduler::DependencyTracker, SchedulerError> {
        return internal::reduce<Chunks>(s, b, e, acc, std::forward<Fn>(fn), [](auto) {});
    }

    template <std::size_t Chunks = 512, typename Acc, typename I, typename Fn>
        requires (std::incrementable<I>)
    auto reduce(
        Scheduler& s,
        I b,
        I e,
        Scheduler::DependencyTracker d,
        Acc acc,
        Fn&& fn
    ) -> std::expected<Scheduler::DependencyTracker, SchedulerError> {
        return internal::reduce<Chunks>(s, b, e, acc, std::forward<Fn>(fn), [d](auto t) {
            return t.deps_on(d);
        });
    }
} // namespace tpl::par

#endif // AMT_TPL_ALGORITHM_HPP
