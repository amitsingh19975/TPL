#ifndef AMT_TPL_ALGORITHM_HPP
#define AMT_TPL_ALGORITHM_HPP

#include "scheduler.hpp"
#include "range.hpp"
#include "task_token.hpp"
#include "tpl/allocator.hpp"
#include "tpl/dyn_array.hpp"
#include <future>
#include <memory>
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

        template <typename T>
        struct ReduceResult {
            ReduceResult(std::size_t n, T acc)
                : m_data(std::make_unique<std::vector<T>>(n, T{}))
                , m_acc(acc)
            {}

            constexpr auto get() const noexcept -> T {
                auto& v = *m_data.get();
                T res = m_acc;
                for (auto i = 0ul; i < v.size(); ++i) {
                    res += v[i];
                }
                return res;
            }

            constexpr auto& data() noexcept {
                return *m_data.get();
            }
        private:
            std::unique_ptr<std::vector<T>> m_data;
            T m_acc;
        };

        template <std::size_t Chunks = 512, typename Acc, typename Fn, bool R>
        auto reduce(
            Scheduler& s,
            Range<R> r,
            Fn&& fn,
            Acc acc,
            auto&& dep_fn
        ) -> std::expected<ReduceResult<Acc>, SchedularError> {
            auto items = (r.size() + Chunks - 1) / Chunks;
            auto result = ReduceResult<Acc>(items, acc);

            for (auto i = 0ul; i < items; ++i) {
                auto start = r.start + i * Chunks * r.stride;
                auto end = std::min(start + r.stride * Chunks, r.end);
                auto range = Range<R>(start, end, r.stride);
                auto t = s.add_task([range, &fn, i, &v=result.data()](auto& t) {
                    if constexpr (std::is_invocable_v<Fn, Range<R>, TaskToken&>) {
                        static_assert(std::is_invocable_r_v<Acc, Fn, Range<R>, TaskToken&>);
                        v[i] = std::invoke(fn, range, t);
                    } else if constexpr (std::is_invocable_v<Fn, TaskToken&, Range<R>>) {
                        static_assert(std::is_invocable_r_v<Acc, Fn, TaskToken&, Range<R>>);
                        v[i] = std::invoke(fn, t, range);
                    } else if constexpr (std::is_invocable_v<Fn, Range<R>>) {
                        static_assert(std::is_invocable_r_v<Acc, Fn, Range<R>>);
                        v[i] = std::invoke(fn, range);
                    } else if constexpr (std::is_invocable_v<Fn, TaskToken&>) {
                        static_assert(std::is_invocable_r_v<Acc, Fn, TaskToken&>);
                        v[i] = std::invoke(fn, t);
                    } else {
                        static_assert(std::is_invocable_r_v<Acc, Fn>);
                        v[i] = std::invoke(fn);
                    }
                });
                using ret_t = decltype(dep_fn(t));
                if constexpr (!std::is_void_v<ret_t>) {
                    auto res = dep_fn(t);
                    if (!res) return std::unexpected(res.error());
                } else {
                    dep_fn(t);
                }
            }

            return std::move(result);
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

    template <std::size_t Chunks = 512, typename Acc, typename Fn, bool R>
    auto reduce(
        Scheduler& s,
        Range<R> r,
        Fn&& fn,
        Acc acc
    ) -> std::expected<internal::ReduceResult<Acc>, SchedularError> {
        return internal::reduce<Chunks>(s, r, std::forward<Fn>(fn), acc, [](auto) {});
    }

    template <std::size_t Chunks = 512, typename Acc, typename Fn, bool R>
    auto reduce(
        Scheduler& s,
        Range<R> r,
        Scheduler::DependencyTracker d,
        Fn&& fn,
        Acc acc
    ) -> std::expected<internal::ReduceResult<Acc>, SchedularError> {
        return internal::reduce<Chunks>(s, r, std::forward<Fn>(fn), acc, [d](auto t) {
            return t.deps_on(d);
        });
    }
} // namespace tpl::par

#endif // AMT_TPL_ALGORITHM_HPP
