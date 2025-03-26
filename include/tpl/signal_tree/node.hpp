#ifndef AMT_TPL_SIGNAL_TREE_NODE_HPP
#define AMT_TPL_SIGNAL_TREE_NODE_HPP

#include "int.hpp"
#include "signal_index.hpp"
#include "../atomic.hpp"
#include <atomic>
#include <bit>
#include <cassert>
#include <print>

namespace tpl::internal {
    struct alignas(atomic::internal::hardware_destructive_interference_size) NodeAlignedWrapper {
        NodeIntTraits::type data;
    };

    template <std::size_t BitsPerNode, std::size_t Extent, std::size_t Stride>
        requires (BitsPerNode < 128)
    struct Node {
        using type = NodeIntTraits::type;
        static constexpr auto mask = (type(1) << BitsPerNode) - 1;
        // This is a upper limit that is not applicable for nodes that are less than max nodes
        static constexpr auto nodes_per_block = NodeIntTraits::max_nodes / BitsPerNode;
        NodeAlignedWrapper* ptr;
        static constexpr auto offset = Stride % NodeIntTraits::max_nodes; 
        static constexpr auto total_nodes = Extent / BitsPerNode;
        static constexpr auto blocks = (Extent + offset + NodeIntTraits::max_nodes - 1) / NodeIntTraits::max_nodes;

        // [3, 2, 1] [6, 5, 4]....
        TPL_ATOMIC_FUNC_ATTR auto get_value(SignalIndex index) const noexcept -> std::size_t {
            auto [block_index, idx] = parse_index(index);
            auto const* block = reinterpret_cast<atomic::Atomic const*>(ptr + block_index);
            auto data = std::bit_cast<type>(block->load(std::memory_order_acquire));
            return static_cast<std::size_t>((data >> idx) & mask);
        }

        TPL_ATOMIC_FUNC_ATTR auto get_data(SignalIndex index) const noexcept -> type {
            auto block_index = (index.index * BitsPerNode + offset) / nodes_per_block;
            auto const* block = reinterpret_cast<atomic::Atomic const*>(ptr + block_index);
            return std::bit_cast<type>(block->load(std::memory_order_acquire));
        }

        TPL_ATOMIC_FUNC_ATTR auto compare_exchange(SignalIndex index, type expected, type new_value) noexcept -> bool {
            auto [block_index, _] = parse_index(index);
            auto* block = reinterpret_cast<atomic::Atomic*>(ptr + block_index);
            return block->compare_exchange(
                std::bit_cast<atomic::Atomic::base_type>(expected),
                std::bit_cast<atomic::Atomic::base_type>(new_value),
                std::memory_order_relaxed,
                std::memory_order_relaxed
            );
        }

        TPL_ATOMIC_FUNC_ATTR auto inc(SignalIndex index) noexcept -> type {
            auto [block_index, idx] = parse_index(index);
            auto* block = reinterpret_cast<atomic::Atomic*>(ptr + block_index);
            auto msk = type(1) << idx;
            while (true) {
                auto data = std::bit_cast<type>(block->load(std::memory_order_acquire));
                auto value = (data >> idx) & mask; 
                if (value == mask) return data;
                auto new_data = data + msk;
                auto res = block->compare_exchange(
                    std::bit_cast<atomic::Atomic::base_type>(data),
                    std::bit_cast<atomic::Atomic::base_type>(new_data),
                    std::memory_order_relaxed,
                    std::memory_order_relaxed
                );
                if (res) return data;
            }
        }

        TPL_ATOMIC_FUNC_ATTR auto dec(SignalIndex index) noexcept -> type {
            auto [block_index, idx] = parse_index(index);
            auto* block = reinterpret_cast<atomic::Atomic*>(ptr + block_index);
            auto inc = std::bit_cast<atomic::Atomic::base_type>(type(1) << idx);
            return std::bit_cast<type>(block->fetch_sub(inc, std::memory_order_release));
        }

        TPL_ATOMIC_FUNC_ATTR auto dec_helper(SignalIndex index, type data) const noexcept -> type {
            auto block_index = (index.index * BitsPerNode + offset) / nodes_per_block;
            auto idx = index.index - (block_index * nodes_per_block);
            idx *= BitsPerNode;
            if (block_index == 0) idx += offset;
            auto inc = type(1) << idx;
            return data - inc;
        }

        TPL_ATOMIC_FUNC_ATTR auto get_value_helper(SignalIndex index, type data) const noexcept -> type {
            auto block_index = (index.index * BitsPerNode + offset) / nodes_per_block;
            auto idx = index.index - (block_index * nodes_per_block);
            idx *= BitsPerNode;
            if (block_index == 0) idx += offset;
            return static_cast<std::size_t>((data >> idx) & mask);
        }

        TPL_ATOMIC_FUNC_ATTR auto set_flag(SignalIndex index, bool flag) noexcept -> type {
            auto [block_index, idx] = parse_index(index);
            auto* block = reinterpret_cast<atomic::Atomic*>(ptr + block_index);
            if (flag) {
                auto msk = std::bit_cast<atomic::Atomic::base_type>(type(1) << idx);
                return std::bit_cast<type>(block->fetch_or(msk, std::memory_order_release));
            } else {
                auto msk = std::bit_cast<atomic::Atomic::base_type>(~(type(1) << idx));
                return std::bit_cast<type>(block->fetch_and(msk, std::memory_order_release));
            }
        }

        void debug_print(bool bin = false) const {
            std::print("{{");

            auto helper = [](
                type data,
                std::size_t offset,
                std::size_t sz,
                bool bin,
                bool comma
            ) {
                std::print("<");
                for (auto j = 0ul; j < sz; ++j) {
                    auto num = (data >> (j * BitsPerNode + offset)) & mask;
                    if (bin) {
                        std::print("[{:0{}b}]", num, BitsPerNode);
                    } else {
                        std::print("[{}]", num);
                    }
                }
                std::print(">");
                if (comma) std::print(", ");
            };

            auto current_offset = offset;
            auto size = (NodeIntTraits::max_nodes - offset) / BitsPerNode;
            for (auto i = 0ul; i < blocks; ++i) {
                helper(
                    ptr[i].data,
                    current_offset,
                    std::min(size, total_nodes),
                    bin,
                    i + 1 != blocks
                );
                current_offset = 0;
                size = (i + 1) * nodes_per_block;
            }

            std::println("}}");
        }

        constexpr auto parse_index(SignalIndex index) const noexcept -> std::pair<std::size_t, std::size_t> {
            auto abs_index = index.index * BitsPerNode + offset;
            auto block_index = abs_index / NodeIntTraits::max_nodes;
            return { block_index, abs_index % NodeIntTraits::max_nodes };
        }
    };
} // namespace tpl::internal

#endif // AMT_TPL_SIGNAL_TREE_NODE_HPP
