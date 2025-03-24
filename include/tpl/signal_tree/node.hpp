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

    template <std::size_t BitsPerNode, std::size_t Blocks, std::size_t TotalNodes, std::size_t Offset>
        requires (BitsPerNode < 128)
    struct Node {
        using type = NodeIntTraits::type;
        static constexpr auto number_of_blocks = Blocks;
        static constexpr auto mask = (type(1) << BitsPerNode) - 1;
        // This is a upper limit that is not applicable for nodes that are less than max nodes
        static constexpr auto nodes_per_block = NodeIntTraits::max_nodes / BitsPerNode;
        NodeAlignedWrapper* ptr;

        // [3, 2, 1] [6, 5, 4]....
        TPL_ATOMIC_FUNC_ATTR auto get_value(SignalIndex index) const noexcept -> std::size_t {
            auto block_index = (index.index * BitsPerNode + Offset) / nodes_per_block;
            auto idx = index.index - (block_index * nodes_per_block);
            idx *= BitsPerNode;
            if (block_index == 0) idx += Offset;
            auto const* block = reinterpret_cast<atomic::Atomic const*>(ptr + block_index);
            auto data = std::bit_cast<type>(block->load(std::memory_order_acquire));
            return static_cast<std::size_t>((data >> idx) & mask);
        }

        TPL_ATOMIC_FUNC_ATTR auto get_data(SignalIndex index) const noexcept -> type {
            auto block_index = (index.index * BitsPerNode + Offset) / nodes_per_block;
            auto const* block = reinterpret_cast<atomic::Atomic const*>(ptr + block_index);
            return std::bit_cast<type>(block->load(std::memory_order_acquire));
        }

        TPL_ATOMIC_FUNC_ATTR auto compare_exchange(SignalIndex index, type expected, type new_value) noexcept -> bool {
            auto block_index = (index.index * BitsPerNode + Offset) / nodes_per_block;
            auto* block = reinterpret_cast<atomic::Atomic*>(ptr + block_index);
            return block->compare_exchange(
                std::bit_cast<atomic::Atomic::base_type>(expected),
                std::bit_cast<atomic::Atomic::base_type>(new_value),
                std::memory_order_relaxed,
                std::memory_order_relaxed
            );
        }

        TPL_ATOMIC_FUNC_ATTR auto inc(SignalIndex index) noexcept -> type {
            auto block_index = (index.index * BitsPerNode + Offset) / nodes_per_block;
            auto idx = index.index - (block_index * nodes_per_block);
            idx *= BitsPerNode;
            if (block_index == 0) idx += Offset;
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
            auto block_index = (index.index * BitsPerNode + Offset) / nodes_per_block;
            auto idx = index.index - (block_index * nodes_per_block);
            idx *= BitsPerNode;
            if (block_index == 0) idx += Offset;
            auto* block = reinterpret_cast<atomic::Atomic*>(ptr + block_index);
            auto inc = std::bit_cast<atomic::Atomic::base_type>(type(1) << idx);
            return std::bit_cast<type>(block->fetch_sub(inc, std::memory_order_release));
        }

        TPL_ATOMIC_FUNC_ATTR auto dec_helper(SignalIndex index, type data) const noexcept -> type {
            auto block_index = (index.index * BitsPerNode + Offset) / nodes_per_block;
            auto idx = index.index - (block_index * nodes_per_block);
            idx *= BitsPerNode;
            if (block_index == 0) idx += Offset;
            auto inc = type(1) << idx;
            return data - inc;
        }

        TPL_ATOMIC_FUNC_ATTR auto get_value_helper(SignalIndex index, type data) const noexcept -> type {
            auto block_index = (index.index * BitsPerNode + Offset) / nodes_per_block;
            auto idx = index.index - (block_index * nodes_per_block);
            idx *= BitsPerNode;
            if (block_index == 0) idx += Offset;
            return static_cast<std::size_t>((data >> idx) & mask);
        }

        TPL_ATOMIC_FUNC_ATTR auto set_flag(SignalIndex index, bool flag) noexcept -> type {
            auto block_index = (index.index * BitsPerNode + Offset) / nodes_per_block;
            auto idx = index.index - (block_index * nodes_per_block);
            idx *= BitsPerNode;
            if (block_index == 0) idx += Offset;
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
            constexpr auto first_size = std::min((NodeIntTraits::max_nodes - Offset) / BitsPerNode, TotalNodes);
            auto total_size = TotalNodes;
            for (auto i = 0ul; i < Blocks; ++i) {
                std::print("<");
                auto data = std::bit_cast<type>(ptr[i].data);
                std::size_t sz{};
                if (i == 0 && Offset) {
                    data >>= Offset;
                    sz = first_size;
                    total_size -= first_size;
                } else {
                    constexpr auto blk = (NodeIntTraits::max_nodes / BitsPerNode);
                    sz = total_size;
                    total_size -= blk;
                }
                for (auto j = 0ul; j < sz; ++j) {
                    auto num = (data >> (j * BitsPerNode)) & mask;
                    if (bin) {
                        std::print("[{:0{}b}]", num, BitsPerNode);
                    } else {
                        std::print("[{}]", num);
                    }
                }
                std::print(">");
                if (i + 1 != Blocks) std::print(", ");
            }
            std::println("}}");
        }
    };
} // namespace tpl::internal

#endif // AMT_TPL_SIGNAL_TREE_NODE_HPP
