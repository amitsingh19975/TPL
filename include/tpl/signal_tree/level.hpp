#ifndef AMT_TPL_SIGNAL_TREE_LEVEL_HPP
#define AMT_TPL_SIGNAL_TREE_LEVEL_HPP

#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
#include "int.hpp"
#include "node.hpp"
#include "../maths.hpp"
#include "tpl/atomic.hpp"
#include "tpl/signal_tree/signal_index.hpp"
#include <functional>
#include <numeric>
#include <print>
#include <utility>

namespace tpl::internal {

    // [[.][..][....]......][...............]
    // Level<1, 64>
    // level 1 => 64
    // level 2 => 32, 32
    // level 3 => 16, 16, 16, 16
    // ...
    // level 7 => 1, 1, .... 1
    // Total Levels => log2(N) + 1
    // Total Bits Required
    //      => bit_width(64) * 1 + bit_width(32) * 2 + ....
    //      => 6 * 1 + 5 * 2 + ....
    //      => sum[from: (l = L, i = 0), to: (l = 1, i = L)](l * 2^i)

    template <std::size_t Cap>
        requires (maths::is_non_zero_power_of_two(Cap))
    struct LevelContainer {
        using type = NodeIntTraits::type;

    private:
        static constexpr auto levels = static_cast<std::size_t>(std::countr_zero(Cap) + 1);
        static constexpr auto extents = [](){
            std::array<std::size_t, levels> res{};
            for (auto i = 0ul; i < levels; ++i) {
                auto sz = (levels - i) * (std::size_t{1} << i);
                if (sz < NodeIntTraits::max_nodes) res[i] = sz;
                else {
                    res[i] = ((sz + NodeIntTraits::max_nodes - 1) / NodeIntTraits::max_nodes) * NodeIntTraits::max_nodes;
                }
            }
            return res;
        }();
        static constexpr auto strides = [](){
            std::array<std::size_t, extents.size()> res{};
            std::transform(extents.begin(), extents.end() - 1, res.begin(), res.begin() + 1, std::plus<>{});
            return res;
        }();
        static constexpr std::size_t total_bits = std::accumulate(extents.begin(), extents.end(), std::size_t{}, std::plus<>{});
        static constexpr std::size_t size = (total_bits + NodeIntTraits::max_nodes - 1) / NodeIntTraits::max_nodes;
    public:

        template <std::size_t L = 0>
        void debug_print(bool bin = false) const {
            if constexpr (L == levels) return;
            else {
                auto const node = get_nodes<L>();
                node.debug_print(bin);
                debug_print<L + 1>();
            }
        }

        TPL_ATOMIC_FUNC_ATTR auto set(
            std::size_t signal_index
        ) noexcept -> std::pair<bool /*WasSet*/, bool /*Successful*/> {
            return set_helper<levels - 1>(SignalIndex(signal_index));
        }

        TPL_ATOMIC_FUNC_ATTR auto set(
            SignalIndex signal_index
        ) noexcept -> std::pair<bool /*WasSet*/, bool /*Successful*/> {
            return set_helper<levels - 1>(signal_index);
        }

        TPL_ATOMIC_FUNC_ATTR auto select() noexcept -> std::pair<SignalIndex /*Index*/, bool /*IsZero*/> {
            return select_helper<0>(SignalIndex(0));
        }

        constexpr auto data() const noexcept -> auto const& {
            return m_data;
        }

        auto empty() const noexcept {
            std::size_t count{};
            for (auto i = 0ul; i < m_data.size(); ++i) {
                count += !(reinterpret_cast<atomic::Atomic const*>(m_data.data() + i)->load(std::memory_order_acquire)).is_zero();
            }
            return count == 0;
        };

        template <std::size_t L>
        constexpr auto get_nodes()
            noexcept -> Node<
                /*BitsPerNode=*/ levels - L,
                /*Blocks=*/ (extents[L] + NodeIntTraits::max_nodes - 1) / NodeIntTraits::max_nodes,
                /*TotalNodes=*/ extents[L] / (levels - L),
                /*Offset=*/ (strides[L] % NodeIntTraits::max_nodes)
            >
        {
            return { m_data.data() + strides[L] / NodeIntTraits::max_nodes };
        }

        template <std::size_t L>
        constexpr auto get_nodes() const noexcept {
            auto* self = const_cast<LevelContainer<Cap>*>(this);
            return self->template get_nodes<L>();
        }

    private:
        template <std::size_t L>
        TPL_ATOMIC_FUNC_ATTR auto set_helper(
            SignalIndex index
        ) noexcept -> std::pair<bool /*WasSet*/, bool /*Successful*/> {
            auto parent = SignalIndex(index.index / 2);
            auto node = get_nodes<L>();
            auto val = node.inc(index);
            if constexpr (L != 0) {
                return set_helper<L - 1>(parent);
            } else {
                return { val == 0, true };
            }
        }

        template <std::size_t L>
        TPL_ATOMIC_FUNC_ATTR auto select_helper(SignalIndex index) noexcept -> std::pair<SignalIndex /*Index*/, bool /*IsZero*/> {
            auto left = SignalIndex(index.index * 2 + 0);
            auto right = SignalIndex(index.index * 2 + 1);
            auto node = get_nodes<L>();
            while (true) {
                auto data = node.get_data(index);
                auto value = node.get_value_helper(index, data);
                if (!value) return { {}, true };
                auto new_data = node.dec_helper(index, data);
                if (node.compare_exchange(index, data, new_data)) {
                    value = node.get_value_helper(index, new_data);
                    if constexpr (L + 1 == levels) return { index, value == 0 };
                    break;
                }
            }

            if constexpr (L < levels - 1) {
                auto l_res = select_helper<L + 1>(left);
                if (!l_res.first.is_invalid()) return l_res;

                auto r_res = select_helper<L + 1>(right);
                if (!r_res.first.is_invalid()) return r_res;
            }
            return { {}, true };
        }
    private:
        std::array<NodeAlignedWrapper, size> m_data{};
    };
} // namespace tpl::internal

#endif // AMT_TPL_SIGNAL_TREE_LEVEL_HPP
