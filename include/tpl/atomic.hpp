#ifndef AMT_TPL_ATOMIC_HPP
#define AMT_TPL_ATOMIC_HPP

#include <cstdint>
#include <bit>
#include <atomic>
#include <new>

#if defined(_MSC_VER)
#define TPL_COMPILER_MSVC
#include <intrin.h>
#include <__msvc_int128.hpp>
#elif defined(__clang__)
#define TPL_COMPILER_CLANG
#elif defined(__GNUC__) || defined(__GNUG__)
#define TPL_COMPILER_GCC
#else
#error "Unknown compiler"
#endif

#if INTPTR_MAX == INT32_MAX
#define TPL_ENVIRONMENT 32
#elif INTPTR_MAX == INT64_MAX
#define TPL_ENVIRONMENT 64
#else
#error "Environment not 32 or 64-bit."
#endif

#ifdef TPL_COMPILER_MSVC
#define TPL_ATOMIC_FUNC_ATTR __forceinline
#else
#define TPL_ATOMIC_FUNC_ATTR [[using gnu: hot, flatten]]
#endif

namespace tpl::atomic {
    namespace internal {
#ifdef __cpp_lib_hardware_interference_size
        using std::hardware_destructive_interference_size;
#else
#if defined(_M_ARM) || defined(_M_ARM64) || defined(__ARM_NEON__)
        static constexpr std::size_t hardware_destructive_interference_size = 128;
#else
        static constexpr std::size_t hardware_destructive_interference_size = 64;
#endif
#endif
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
        static inline constexpr auto from_mem_order(std::memory_order order) noexcept {
            switch (order) {
            case std::memory_order::relaxed: return __ATOMIC_RELAXED;
            case std::memory_order::consume: return __ATOMIC_CONSUME;
            case std::memory_order::acquire: return __ATOMIC_ACQUIRE;
            case std::memory_order::release: return __ATOMIC_RELEASE;
            case std::memory_order::acq_rel: return __ATOMIC_ACQ_REL;
            case std::memory_order::seq_cst: return __ATOMIC_SEQ_CST;
                break;
            }
        }
#endif
    } // namespace internal

    struct Atomic {
        using int_t = std::uintptr_t;
        struct alignas(alignof(int_t)) Int {
            int_t first{};
            int_t second{};

            constexpr auto is_zero() const noexcept -> bool {
            #if defined(__SIZEOF_INT128__)
                return std::bit_cast<__uint128_t>(*this) == 0;
            #else
                return first == 0 && second == 0;
            #endif
            }
        };

        using base_type = Int;
        constexpr Atomic() noexcept = default;
        constexpr Atomic(Atomic const&) noexcept = default;
        constexpr Atomic(Atomic&&) noexcept = default;
        constexpr Atomic& operator=(Atomic const&) noexcept = default;
        constexpr Atomic& operator=(Atomic&&) noexcept = default;
        constexpr ~Atomic() noexcept = default;

        constexpr Atomic(int_t first, int_t second) noexcept
            : data{ first, second }
        {
        }
        constexpr Atomic(base_type value) noexcept
            : data(value)
        {
        }

        TPL_ATOMIC_FUNC_ATTR auto load(std::memory_order order = std::memory_order_seq_cst) noexcept -> base_type {
#if TPL_ENVIRONMENT == 32
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
            auto ord = internal::from_mem_order(order);
            return std::bit_cast<base_type>(__atomic_load_n(&scalar, ord));
#else
            return std::bit_cast<base_type>(reinterpret_cast<std::atomic<std::uint64_t>*>(this)->load(order));
#endif
#else
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
            auto ord = internal::from_mem_order(order);
            __uint128_t tmp = __atomic_load_n(&scalar, ord);
            return std::bit_cast<base_type>(tmp);
#else
            alignas(16) int64_t result[2] = { scalar[0], scalar[1] };

            do {
                result[0] = scalar[0];
                result[1] = scalar[1];
            } while (!_InterlockedCompareExchange128(scalar, result[1], result[0], result));

            return std::bit_cast<base_type>(result);
#endif
#endif
        }

        TPL_ATOMIC_FUNC_ATTR auto load(std::memory_order order = std::memory_order_seq_cst) const noexcept -> base_type {
            auto self = const_cast<Atomic*>(this);
            return self->load(order);
        }

        TPL_ATOMIC_FUNC_ATTR auto store(base_type value, std::memory_order order = std::memory_order_seq_cst) noexcept -> void {
#if TPL_ENVIRONMENT == 32
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
            auto ord = internal::from_mem_order(order);
            __atomic_store_n(&scalar, std::bit_cast<std::uint64_t>(value), ord);
#else
            reinterpret_cast<std::atomic<std::uint64_t>*>(this)->store(std::bit_cast<std::uint64_t>(value), order);
#endif
#else
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
            auto ord = internal::from_mem_order(order);
            __atomic_store_n(&scalar, std::bit_cast<__uint128_t>(value), ord);
#else
            std::int64_t expected_low, expected_high;
            auto arr = reinterpret_cast<const std::int64_t*>(&value);
            std::int64_t new_low = arr[0];
            std::int64_t new_high = arr[1];

            do {
                expected_low = scalar[0];
                expected_high = scalar[1];
            } while (!_InterlockedCompareExchange128(scalar, new_high, new_low, &expected_low));
#endif
#endif
        }

        TPL_ATOMIC_FUNC_ATTR auto compare_exchange(
            base_type expected,
            base_type new_value,
            [[maybe_unused]] std::memory_order success = std::memory_order_seq_cst,
            [[maybe_unused]] std::memory_order failure = std::memory_order_seq_cst
        ) noexcept -> bool {
#if TPL_ENVIRONMENT == 32
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
            auto e = std::bit_cast<std::uint64_t>(expected);
            auto n = std::bit_cast<std::uint64_t>(new_value);
            return __atomic_compare_exchange_n(&scalar, &e, n, false, internal::from_mem_order(success), internal::from_mem_order(failure));
#else
            auto e = std::bit_cast<std::uint64_t>(expected);
            auto n = std::bit_cast<std::uint64_t>(new_value);
            return reinterpret_cast<std::atomic<std::uint64_t>*>(this)->compare_exchange_strong(e, n, success, failure);
#endif
#else
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
            auto e = std::bit_cast<__uint128_t>(expected);
            auto n = std::bit_cast<__uint128_t>(new_value);
            return __atomic_compare_exchange(&scalar, &e, &n, false, internal::from_mem_order(success), internal::from_mem_order(failure));
#else
            alignas(16) int64_t expected_array[2] = { expected.first, expected.second };
            alignas(16) int64_t new_array[2] = { new_value.first, new_value.second };

            return _InterlockedCompareExchange128(
                scalar,
                new_array[1],
                new_array[0],
                expected_array
            ) == 1;
#endif
#endif
        }

        TPL_ATOMIC_FUNC_ATTR auto fetch_add(
            base_type new_value,
            [[maybe_unused]] std::memory_order order = std::memory_order_seq_cst
        ) noexcept -> base_type {
#if TPL_ENVIRONMENT == 32
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
            auto n = std::bit_cast<std::uint64_t>(new_value);
            return std::bit_cast<base_type>(__atomic_fetch_add(&scalar, n, internal::from_mem_order(order)));
#else
            auto n = std::bit_cast<std::uint64_t>(new_value);
            return reinterpret_cast<std::atomic<std::uint64_t>*>(this)->fetch_add(n, order);
#endif
#else
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
            auto n = std::bit_cast<__uint128_t>(new_value);
            return std::bit_cast<base_type>(__atomic_fetch_add(&scalar, n, internal::from_mem_order(order)));
#else
            alignas(16) std::_Unsigned128 expected_array = std::bit_cast<std::_Unsigned128>(data);
            alignas(16) auto new_array = expected_array;

            do {
                expected_array = std::bit_cast<std::_Unsigned128>(scalar);
                new_array = expected_array + std::bit_cast<std::_Unsigned128>(new_value);
            } while (!_InterlockedCompareExchange128(
                scalar,
                static_cast<std::int64_t>(new_array._Word[0]),
                static_cast<std::int64_t>(new_array._Word[1]),
                reinterpret_cast<std::int64_t*>(&expected_array)
            ));

            return std::bit_cast<base_type>(expected_array);
#endif
#endif
        }

        TPL_ATOMIC_FUNC_ATTR auto fetch_sub(
            base_type sub_value,
            [[maybe_unused]] std::memory_order order = std::memory_order_seq_cst
        ) noexcept -> base_type {
#if TPL_ENVIRONMENT == 32
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
            auto n = std::bit_cast<std::uint64_t>(sub_value);
            return std::bit_cast<base_type>(__atomic_fetch_sub(&scalar, n, internal::from_mem_order(order)));
#else
            auto n = std::bit_cast<std::uint64_t>(sub_value);
            return reinterpret_cast<std::atomic<std::uint64_t>*>(this)->fetch_sub(n, order);
#endif
#else
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
            auto n = std::bit_cast<__uint128_t>(sub_value);
            return std::bit_cast<base_type>(__atomic_fetch_sub(&scalar, n, internal::from_mem_order(order)));
#else
            alignas(16) std::_Unsigned128 expected_array = std::bit_cast<std::_Unsigned128>(data);
            alignas(16) auto new_array = expected_array;

            do {
                expected_array = std::bit_cast<std::_Unsigned128>(scalar);
                new_array = expected_array - std::bit_cast<std::_Unsigned128>(sub_value);
            } while (!_InterlockedCompareExchange128(
                scalar,
                static_cast<std::int64_t>(new_array._Word[0]),
                static_cast<std::int64_t>(new_array._Word[1]),
                reinterpret_cast<std::int64_t*>(&expected_array)
            ));

            return std::bit_cast<base_type>(expected_array);
#endif
#endif
        }

        TPL_ATOMIC_FUNC_ATTR auto fetch_or(
            base_type value,
            [[maybe_unused]] std::memory_order order = std::memory_order_seq_cst
        ) noexcept -> base_type {
#if TPL_ENVIRONMENT == 32
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
            auto n = std::bit_cast<std::uint64_t>(value);
            return std::bit_cast<base_type>(__atomic_fetch_or(&scalar, n, internal::from_mem_order(order)));
#else
            auto n = std::bit_cast<std::uint64_t>(value);
            return reinterpret_cast<std::atomic<std::uint64_t>*>(this)->fetch_or(n, order);
#endif
#else
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
            auto n = std::bit_cast<__uint128_t>(value);
            auto tmp = __atomic_fetch_or(&scalar, n, internal::from_mem_order(order));
            return std::bit_cast<base_type>(tmp);
#else
            alignas(16) std::_Unsigned128 expected_array = std::bit_cast<std::_Unsigned128>(data);
            alignas(16) auto new_array = expected_array;

            do {
                expected_array = std::bit_cast<std::_Unsigned128>(scalar);
                new_array = expected_array | std::bit_cast<std::_Unsigned128>(value);
            } while (!_InterlockedCompareExchange128(
                scalar,
                static_cast<std::int64_t>(new_array._Word[0]),
                static_cast<std::int64_t>(new_array._Word[1]),
                reinterpret_cast<std::int64_t*>(&expected_array)
            ));

            return std::bit_cast<base_type>(expected_array);
#endif
#endif
        }

        TPL_ATOMIC_FUNC_ATTR auto fetch_and(
            base_type value,
            [[maybe_unused]] std::memory_order order = std::memory_order_seq_cst
        ) noexcept -> base_type {
#if TPL_ENVIRONMENT == 32
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
            auto n = std::bit_cast<std::uint64_t>(value);
            return std::bit_cast<base_type>(__atomic_fetch_and(&scalar, n, internal::from_mem_order(order)));
#else
            auto n = std::bit_cast<std::uint64_t>(value);
            return reinterpret_cast<std::atomic<std::uint64_t>*>(this)->fetch_and(n, order);
#endif
#else
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
            auto n = std::bit_cast<__uint128_t>(value);
            return std::bit_cast<base_type>(__atomic_fetch_and(&scalar, n, internal::from_mem_order(order)));
#else
            alignas(16) std::_Unsigned128 expected_array = std::bit_cast<std::_Unsigned128>(data);
            alignas(16) auto new_array = expected_array;

            do {
                expected_array = std::bit_cast<std::_Unsigned128>(scalar);
                new_array = expected_array & std::bit_cast<std::_Unsigned128>(value);
            } while (!_InterlockedCompareExchange128(
                scalar,
                static_cast<std::int64_t>(new_array._Word[0]),
                static_cast<std::int64_t>(new_array._Word[1]),
                reinterpret_cast<std::int64_t*>(&expected_array)
            ));

            return std::bit_cast<base_type>(expected_array);
#endif
#endif
        }

        union alignas(sizeof(base_type)) {
#if TPL_ENVIRONMENT == 32
            mutable std::uint64_t scalar;
#else
#if defined(TPL_COMPILER_CLANG) || defined(TPL_COMPILER_GCC)
            mutable __uint128_t scalar;
#else
            std::int64_t scalar[2];
#endif
#endif
            base_type data{};
        };
    };

    static_assert(sizeof(Atomic) * 8 == TPL_ENVIRONMENT * 2);

} // namespace tpl::atomic


#if defined(__MSC_VER)
#undef TPL_COMPILER_MSVC
#elif defined(__clang__)
#undef TPL_COMPILER_CLANG
#elif defined(__GNUC__) || defined(__GNUG__)
#undef TPL_COMPILER_GCC
#endif

#undef TPL_ENVIRONMENT

#endif // AMT_TPL_ATOMIC_HPP
