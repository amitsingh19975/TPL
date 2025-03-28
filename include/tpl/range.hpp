#ifndef AMT_TPL_RANGE_HPP
#define AMT_TPL_RANGE_HPP

#include <algorithm>
#include <cstddef>

namespace tpl {

    // Range(a, b): [a, b)
    template <bool Reversed = false>
    struct Range {
        using size_type = std::size_t;
        using value_t = std::size_t;

        value_t start{};
        value_t end{};
        value_t stride{1};

        constexpr Range(value_t s) noexcept
            : Range(s, s + 1, 1)
        {}

        constexpr Range(value_t s, value_t e, value_t step = value_t{1}) noexcept
            : start(s)
            , end(std::max(s, e))
            , stride(step)
        {}

        constexpr Range(Range const&) noexcept = default;
        constexpr Range(Range &&) noexcept = default;
        constexpr Range& operator=(Range const&) noexcept = default;
        constexpr Range& operator=(Range &&) noexcept = default;
        constexpr ~Range() noexcept = default;

        constexpr auto size() const noexcept -> size_type {
            return (end - start + stride - 1) / stride;
        }

        constexpr auto apply_step(value_t iter) const noexcept -> value_t {
            return Reversed ? iter - stride : iter + stride;
        }
    };

    using range_t = Range<false>;
    using rev_range_t = Range<true>;
} // namespace tpl

#endif // AMT_TPL_RANGE_HPP
