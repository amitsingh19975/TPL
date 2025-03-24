#ifndef AMT_TPL_MATHS_HPP
#define AMT_TPL_MATHS_HPP

#include <cstddef>

namespace tpl::maths {
    constexpr auto is_power_of_two(std::size_t size) noexcept -> bool {
        return (size == 0) || (size & (size - 1)) == 0;
    }

    constexpr auto is_non_zero_power_of_two(std::size_t size) noexcept -> bool {
        return (size != 0) && ((size & (size - 1)) == 0);
    }
} // namespace tpl::maths

#endif // AMT_TPL_MATHS_HPP
