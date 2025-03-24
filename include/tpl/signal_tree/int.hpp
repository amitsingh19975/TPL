#ifndef AMT_TPL_INT_HPP
#define AMT_TPL_INT_HPP

#include "../atomic.hpp"
#include <bit>
#include <cstdint>

#define TPL_WIN_128 0
#define TPL_GNU_128 1

#if defined(_MSC_VER)
    #if __has_include(<__msvc_int128.hpp>)
        #include <__msvc_int128.hpp>
        #define TPL_INT TPL_WIN_128
    #endif
#elif defined(__SIZEOF_INT__)
    #define TPL_INT TPL_GNU_128
#endif

namespace tpl::internal {
    struct NodeIntTraits {
    #if TPL_INT == TPL_WIN_128
        using u128_t = std::_Unsigned128;
    #elif TPL_INT == TPL_GNU_128 
        using u128_t = __uint128_t;
    #else
        using u128_t = void;
    #endif
        using type = std::conditional_t<
            (sizeof(atomic::Atomic) == 16) &&
            !std::is_void_v<u128_t>,
            u128_t,
            std::size_t
        >;
        using index_t = std::uint64_t;
        static constexpr auto max_nodes = sizeof(type) * 8;

        static constexpr auto sub_counter_arity(type cap, std::size_t n = max_nodes) noexcept -> std::size_t {
            while (n) {
                auto bits_required = bit_width(cap / n) * n;
                if (bits_required <= max_nodes) return n;
                n /= 2;
            }
            return n;
        }

        static constexpr auto bit_width(type val) noexcept -> std::size_t {
            auto count = std::size_t{};
            while (val) {
                val >>= 1;
                ++count;
            }
            return count;
        }

        template <typename T>
            requires (sizeof(T) < 16)
        static constexpr auto bit_width(T val) noexcept -> std::size_t {
            return max_nodes - static_cast<std::size_t>(std::countl_zero(val));
        }
    };

} // namespace tpl::internal

#undef TPL_WIN_128
#undef TPL_GNU_128
#undef TPL_INT

#endif // AMT_TPL_INT_HPP
