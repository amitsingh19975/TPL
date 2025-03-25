#ifndef AMT_TPL_BASIC_HPP
#define AMT_TPL_BASIC_HPP

#include <cstdint>

namespace tpl {

    namespace internal {
        template <unsigned N>
        struct storage_value;

        template <>
        struct storage_value<1> { using type = std::uint8_t; };

        template <>
        struct storage_value<2> { using type = std::uint16_t; };

        template <>
        struct storage_value<4> { using type = std::uint32_t; };

        template <>
        struct storage_value<8> { using type = std::uint64_t; };
    } // namespace internal

} // namespace tpl

#endif // AMT_TPL_BASIC_HPP


