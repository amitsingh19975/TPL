#ifndef AMT_TPL_SIGNAL_TREE_SIGNAL_INDEX_HPP
#define AMT_TPL_SIGNAL_TREE_SIGNAL_INDEX_HPP

#include "int.hpp"
#include <limits>

namespace tpl {

    struct SignalIndex {
        using value_t = internal::NodeIntTraits::type;
        static constexpr auto invalid = std::numeric_limits<std::size_t>::max();
        std::size_t index{invalid};

        constexpr auto is_invalid() const noexcept -> bool {
            return index == invalid;
        }

        constexpr operator bool() const noexcept {
            return !is_invalid();
        }

        constexpr auto mask() const noexcept -> value_t {
            using namespace internal;
            return (value_t(1) << (NodeIntTraits::max_nodes - 1)) >> index;
        }

        constexpr auto get(value_t val) const noexcept -> bool {
            return val & mask();
        }

        constexpr auto set(value_t val, bool flag) const noexcept -> value_t {
            if (flag) return val | mask();
            else return val & ~mask();
        }
    };
} // namespace tpl

#endif // AMT_TPL_SIGNAL_TREE_SIGNAL_INDEX_HPP
