#ifndef AMT_TPL_ALGORITHM_HPP
#define AMT_TPL_ALGORITHM_HPP

#include "schedular.hpp"
#include "range.hpp"
#include "tpl/task_token.hpp"
#include <type_traits>

namespace tpl::par {

    template <std::size_t Chunks = 512, typename Fn, bool R>
    auto for_each(Schedular& s, Range<R> r, Fn&& fn) {
        auto items = (r.size() + Chunks - 1) / Chunks;
        for (auto i = 0ul; i < items; ++i) {
            auto start = r.start + i * Chunks * r.stride;
            auto end = std::min(start + r.stride * Chunks, r.end);
            auto range = Range<R>(start, end, r.stride);
            s.add_task([range, &fn]([[maybe_unused]] auto& t) {
                if constexpr (std::is_invocable_v<Fn, Range<R>>) {
                    std::invoke(fn, range);
                } else {
                    std::invoke(&fn);
                }
            });
        }
    }

    template <std::size_t Chunks = 512, typename Fn, bool R>
    auto for_each(
        Schedular& s,
        Range<R> r,
        Schedular::DependencyTracker d,
        Fn&& fn
    )  -> std::expected<void, SchedularError> {
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
            auto res = t.deps_on(d);
            if (!res) return res;
        }
        return {};
    }
} // namespace tpl::par

#endif // AMT_TPL_ALGORITHM_HPP
