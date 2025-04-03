#ifndef AMT_TPL_LINK_LIST_HPP
#define AMT_TPL_LINK_LIST_HPP

#include "atomic.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace tpl {

    template <typename T, std::size_t BlockSize = 128>
    struct BlockSizedList {
        using value_type = T;
        using size_type = std::size_t;
        using reference = value_type&;
        using const_reference = value_type const&;

        static constexpr auto block_size = BlockSize;

    private:
        struct Node {
            std::array<T, BlockSize> data;
            alignas(atomic::internal::hardware_destructive_interference_size) std::atomic<Node*> next{nullptr};
            std::atomic<std::size_t> size{0};
        };
    public:
        template <bool IsConst>
        struct Iterator {
            using iterator_category = std::forward_iterator_tag;
            using value_type        = T;
            using difference_type   = std::ptrdiff_t;
            using pointer           = T*;
            using reference         = T&;
            using node_t = std::conditional_t<IsConst, Node const*, Node*>;

            constexpr Iterator(node_t data, std::size_t index = 0) noexcept
                : m_node(data)
                , m_index(index)
            {}
            constexpr Iterator(Iterator const&) noexcept = default;
            constexpr Iterator(Iterator &&) noexcept = default;
            constexpr Iterator& operator=(Iterator const&) noexcept = default;
            constexpr Iterator& operator=(Iterator &&) noexcept = default;
            constexpr ~Iterator() noexcept = default;

            constexpr reference operator*() const noexcept {
                assert(m_node != nullptr);
                return m_node->data[m_index];
            }

            constexpr pointer operator->() const  noexcept{
                return &m_node->data[m_index];
            }

            constexpr Iterator& operator++() noexcept {
                auto next = m_index + 1;
                auto const bidx = next / block_size;
                m_index = next % block_size;
                if (bidx != 0) m_node = m_node->next.load(std::memory_order_relaxed);
                return *this;
            }

            constexpr Iterator& operator++(int) noexcept {
                auto tmp = *this;
                ++(*this);
                return tmp;
            }
            constexpr auto operator==(Iterator const&) const noexcept -> bool = default;
            constexpr auto operator!=(Iterator const&) const noexcept -> bool = default;
        private:
            node_t m_node{};
            std::size_t m_index{};
        };

        using iterator = Iterator<false>;
        using const_iterator = Iterator<true>;

        void push_back(value_type val) noexcept(std::is_nothrow_move_assignable_v<value_type>) {
            auto node = new Node();
            node->data[0] = std::move(val);
            node->size = 1;

            while (true) {
                Node* head = m_head.load(std::memory_order_acquire);
                if (head) {
                    if (try_push_element(head, node->data[0])) {
                        delete node;
                        break;
                    }
                }
                if (m_head.compare_exchange_weak(head, node)) {
                    if (head) {
                        // old head -> new head
                        head->next.store(node);
                    }
                    auto idx = m_count.fetch_add(1);
                    if (idx < m_cache.size()) m_cache[idx] = node;
                    break;
                }
            }
            while (true) {
                Node* tail = m_tail.load(std::memory_order_acquire);
                if (tail != nullptr) {
                    break;
                }
                auto head = m_head.load(std::memory_order_acquire);
                if (m_tail.compare_exchange_weak(tail, head)) break;
            }
        }

        constexpr auto operator[](size_type k) noexcept -> reference {
            auto [b_idx, pos] = map_index(k);
            assert(b_idx < m_count.load(std::memory_order_relaxed));

            if (b_idx < m_cache.size()) return m_cache[b_idx]->data[pos];
            Node* last = m_cache.back();
            b_idx -= m_cache.size() - 1;
            while (last && b_idx != 0) {
                last = last->next.load(std::memory_order_relaxed);
                b_idx -= 1;
            }
            return last->data[pos];
        }

        constexpr auto operator[](size_type k) const noexcept -> const_reference {
            auto self = const_cast<BlockSizedList*>(this);
            return self->operator[](k);
        }

        constexpr auto size() const noexcept -> size_type {
            Node* head = m_head.load(std::memory_order_acquire);
            if (!head) return 0;
            auto last_size = head->size.load(std::memory_order_relaxed);
            return (m_count.load(std::memory_order_relaxed) - 1) * BlockSize + last_size;
        }

        constexpr auto empty() const noexcept -> bool {
            return size() == 0;
        }

        constexpr auto nblocks() const noexcept -> size_type {
            return m_count.load(std::memory_order_relaxed);
        }

        template <typename Fn>
        constexpr auto for_each(Fn&& fn) noexcept -> void {
            auto i = 0ul;
            auto iter = [&i, &fn](Node& node) {
                auto sz = node.size.load(std::memory_order_relaxed);
                for (auto k = 0ul; k < sz; ++k, ++i) {
                    if constexpr (std::invocable<Fn, reference> || std::invocable<Fn, const_reference>) {
                        fn(node.data[k]);
                    } else if constexpr (std::invocable<Fn, reference, std::size_t> || std::invocable<Fn, const_reference, std::size_t>) {
                        fn(node.data[k], i);
                    } else if constexpr (std::invocable<Fn, std::size_t>){
                        fn(i);
                    }
                }
            };
            auto count = m_count.load(std::memory_order_relaxed);
            auto sz = std::min(count, m_cache.size());
            for (auto j = 0ul; j < sz; ++j) {
                iter(*m_cache[j]);
            }
            Node* root = m_cache[m_cache.size() - 1];
            if (!root) return;
            root = root->next.load(std::memory_order_relaxed);
            while (root) {
                root = root->next.load(std::memory_order_relaxed);
            }
        }

        template <typename Fn>
        constexpr auto for_each(Fn&& fn) const noexcept -> void {
            auto i = 0ul;
            auto iter = [&i, &fn](Node& node) {
                auto sz = node.size.load(std::memory_order_relaxed);
                for (auto k = 0ul; k < sz; ++k, ++i) {
                    if constexpr (std::invocable<Fn, reference> || std::invocable<Fn, const_reference>) {
                        fn(node.data[k]);
                    } else if constexpr (std::invocable<Fn, reference, std::size_t> || std::invocable<Fn, const_reference, std::size_t>) {
                        fn(node.data[k], i);
                    } else if constexpr (std::invocable<Fn, std::size_t>){
                        fn(i);
                    }
                }
            };
            auto count = m_count.load(std::memory_order_relaxed);
            auto sz = std::min(count, m_cache.size());
            for (auto j = 0ul; j < sz; ++j) {
                iter(*m_cache[j]);
            }
            Node* root = m_cache[m_cache.size() - 1];
            if (!root) return;
            root = root->next.load(std::memory_order_relaxed);
            while (root) {
                root = root->next.load(std::memory_order_relaxed);
            }
        }

        void resize(size_type count) {
            while (count > size()) {
                push_back({});
            }
        }

        void reset(value_type def) {
            auto sz = size();
            for (auto i = 0ul; i < sz; ++i) {
                this->operator[](i) = def;
            }
        }

        // INFO: Not thread safe
        void clear() {
            Node* tail = m_tail.load(std::memory_order_relaxed);
            while (tail) {
                Node* next = tail->next.load(std::memory_order_relaxed);
                delete tail;
                tail = next;
            }
            m_tail = nullptr;
            m_head = nullptr;
            m_count = 0;
            for(auto i = 0ul; i < m_cache.size(); ++i) m_cache[i] = nullptr;
        }

        constexpr auto begin() noexcept -> iterator {
            return { m_tail.load(std::memory_order_relaxed), 0 };
        }

        constexpr auto end() noexcept -> iterator {
            Node* node = m_head.load(std::memory_order_relaxed);
            if (!node) return { nullptr };
            auto sz = node->size.load(std::memory_order_relaxed);
            if (sz == block_size) return { nullptr };
            return { node, sz };
        }

        constexpr auto begin() const noexcept -> const_iterator {
            return { m_tail.load(std::memory_order_relaxed), 0 };
        }

        constexpr auto end() const noexcept -> const_iterator {
            Node const* node = m_head.load(std::memory_order_relaxed);
            if (!node) return { nullptr };
            auto sz = node->size.load(std::memory_order_relaxed);
            if (sz == block_size) return { nullptr };
            return { node, sz };
        }
    private:
        constexpr auto try_push_element(Node* node, reference val) noexcept(std::is_nothrow_move_assignable_v<value_type>) -> bool {
            auto idx = node->size.fetch_add(1);
            if (idx >= BlockSize) {
                node->size.store(BlockSize, std::memory_order_relaxed);
                return false;
            }
            node->data[idx] = std::move(val);
            return true;
        }

        constexpr auto map_index(size_type k) const noexcept -> std::pair<size_type, size_type> {
            return {
                k / BlockSize, // Block Index
                k % BlockSize, // position
            };
        }

    private:
        std::atomic<Node*> m_tail{};
        alignas(atomic::internal::hardware_destructive_interference_size) std::atomic<Node*> m_head{};
        std::atomic<std::size_t> m_count{};
        std::array<Node*, 64ul> m_cache{}; // keep cache for fast look up
    };

} // namespace tpl

#endif // AMT_TPL_LINK_LIST_HPP
