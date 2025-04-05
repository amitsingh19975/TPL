#ifndef AMT_TPL_LINK_LIST_HPP
#define AMT_TPL_LINK_LIST_HPP

#include "atomic.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory_resource>
#include <optional>
#include <type_traits>
#include <utility>

namespace tpl {

    namespace internal {

    } // namespace internal

    template <typename T, std::size_t BlockSize = 128>
    struct BlockSizedList {
        using value_type = T;
        using size_type = std::size_t;
        using reference = value_type&;
        using const_reference = value_type const&;

        static constexpr auto block_size = BlockSize;

    private:
        struct ListNode {
            std::array<T, BlockSize> data;
            alignas(atomic::internal::hardware_destructive_interference_size) std::atomic<ListNode*> next{nullptr};
            std::atomic<std::size_t> size{0};
        };
        using node_t = ListNode;
    public:
        template <bool IsConst>
        struct Iterator {
            using iterator_category = std::forward_iterator_tag;
            using value_type        = T;
            using difference_type   = std::ptrdiff_t;
            using pointer           = T*;
            using reference         = T&;
            using node_t = std::conditional_t<IsConst, node_t const*, node_t*>;

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

        BlockSizedList() noexcept = default;
        BlockSizedList(BlockSizedList const&) noexcept = delete;
        BlockSizedList(BlockSizedList &&) noexcept = delete;
        BlockSizedList& operator=(BlockSizedList const&) noexcept = delete;
        BlockSizedList& operator=(BlockSizedList &&) noexcept = delete;
        ~BlockSizedList() noexcept = default;

        BlockSizedList(std::pmr::polymorphic_allocator<std::byte> alloc) noexcept
            : m_alloc(std::move(alloc))
        {}

        void push_back(value_type val) noexcept(std::is_nothrow_move_assignable_v<value_type>) {
            auto* node = m_alloc.new_object<node_t>();
            node->data[0] = std::move(val);
            node->size = 1;

            while (true) {
                node_t* head = m_head.load(std::memory_order_acquire);
                if (head) {
                    if (try_push_element(head, node->data[0])) {
                        m_alloc.delete_object(node);
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
                node_t* tail = m_tail.load(std::memory_order_acquire);
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
            node_t* last = m_cache.back();
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
            node_t* head = m_head.load(std::memory_order_acquire);
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
            auto iter = [&i, &fn](node_t& node) {
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
            node_t* root = m_cache[m_cache.size() - 1];
            if (!root) return;
            root = root->next.load(std::memory_order_relaxed);
            while (root) {
                root = root->next.load(std::memory_order_relaxed);
            }
        }

        template <typename Fn>
        constexpr auto for_each(Fn&& fn) const noexcept -> void {
            auto i = 0ul;
            auto iter = [&i, &fn](node_t& node) {
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
            node_t* root = m_cache[m_cache.size() - 1];
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
            for_each([&def](value_type& v) {
                v = def;
            });
        }

        // INFO: Not thread safe
        void clear() {
            node_t* tail = m_tail.load(std::memory_order_relaxed);
            while (tail) {
                node_t* next = tail->next.load(std::memory_order_relaxed);
                m_alloc.delete_object(tail);
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
            node_t* node = m_head.load(std::memory_order_relaxed);
            if (!node) return { nullptr };
            auto sz = node->size.load(std::memory_order_relaxed);
            if (sz == block_size) return { nullptr };
            return { node, sz };
        }

        constexpr auto begin() const noexcept -> const_iterator {
            return { m_tail.load(std::memory_order_relaxed), 0 };
        }

        constexpr auto end() const noexcept -> const_iterator {
            node_t const* node = m_head.load(std::memory_order_relaxed);
            if (!node) return { nullptr };
            auto sz = node->size.load(std::memory_order_relaxed);
            if (sz == block_size) return { nullptr };
            return { node, sz };
        }
    private:
        constexpr auto try_push_element(node_t* node, reference val) noexcept(std::is_nothrow_move_assignable_v<value_type>) -> bool {
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
        std::atomic<node_t*> m_tail{};
        alignas(atomic::internal::hardware_destructive_interference_size) std::atomic<node_t*> m_head{};
        std::atomic<std::size_t> m_count{};
        std::array<node_t*, 64ul> m_cache{}; // keep cache for fast look up
        std::pmr::polymorphic_allocator<std::byte> m_alloc;
    };

    template <typename T, std::size_t BlockSize = sizeof(std::size_t) * 8>
        requires (BlockSize <= sizeof(std::size_t) * 8)
    struct HeadonlyBlockSizedList {
        using value_type = T;
        using size_type = std::size_t;
        using reference = value_type&;
        using const_reference = value_type const&;

        static constexpr auto block_size = BlockSize;

    private:
        struct Node {
            std::array<T, BlockSize> data;
            alignas(atomic::internal::hardware_destructive_interference_size) std::atomic<Node*> next{nullptr};
            std::atomic<std::size_t> in_use{};
        };
        static constexpr auto full = std::numeric_limits<std::size_t>::max() >> (sizeof(std::size_t) * 8 - block_size);

    public:
        struct Index {
            constexpr Index() noexcept = default;
            constexpr Index(Index const&) noexcept = default;
            constexpr Index(Index &&) noexcept = default;
            constexpr Index& operator=(Index const&) noexcept = default;
            constexpr Index& operator=(Index &&) noexcept = default;
            constexpr ~Index() noexcept = default;

            constexpr Index(Node* n, std::size_t pos) noexcept
                : m_node(n)
                , m_pos(pos)
            {}

            constexpr auto empty() const noexcept -> bool { return m_node == nullptr; }
            constexpr auto operator==(Index const&) const noexcept -> bool = default;

            constexpr operator bool() const noexcept { return !empty(); }

            auto take() noexcept(std::is_nothrow_move_assignable_v<value_type>) -> std::optional<value_type> {
                Node* root = std::exchange(m_node, nullptr);
                if (root == nullptr) return {};
                auto mask = std::size_t{1} << m_pos;
                root->in_use.fetch_and(~mask);
                return std::move(root->data[m_pos]);
            }

            auto value() noexcept(std::is_nothrow_copy_assignable_v<value_type>) -> std::optional<value_type> {
                Node* root = m_node;
                if (root == nullptr) return {};
                return root->data[m_pos];
            }

            auto as_ptr() noexcept(std::is_nothrow_copy_assignable_v<value_type>) -> value_type* {
                Node* root = m_node;
                if (root == nullptr) return nullptr;
                return root->data.data() + m_pos;
            }

            auto mark_delete() noexcept -> void {
                auto root = m_node;
                auto const mask = std::size_t{1} << m_pos;
                root->in_use.fetch_and(~mask);
            }

            auto mark_delete(value_type v) noexcept -> void {
                auto root = m_node;
                auto const mask = std::size_t{1} << m_pos;
                root->data[m_pos] = v;
                root->in_use.fetch_and(~mask);
            }
        private:
            Node* m_node{nullptr};
            std::size_t m_pos{};
        };

        HeadonlyBlockSizedList() noexcept = default;
        HeadonlyBlockSizedList(std::pmr::polymorphic_allocator<std::byte> alloc) noexcept
            : m_alloc(std::move(alloc))
        {}
        HeadonlyBlockSizedList(HeadonlyBlockSizedList const&) = delete;
        HeadonlyBlockSizedList(HeadonlyBlockSizedList && other) noexcept = delete;
        HeadonlyBlockSizedList& operator=(HeadonlyBlockSizedList const&) = delete;
        HeadonlyBlockSizedList& operator=(HeadonlyBlockSizedList &&) noexcept = delete;
        ~HeadonlyBlockSizedList() {
            Node* root = m_head.load(std::memory_order_relaxed);
            destroy(root);
        }

        auto push(value_type val) noexcept(std::is_nothrow_move_assignable_v<value_type>) -> Index {
            Node* node = m_alloc.new_object<Node>();
            node->data[0] = std::move(val);
            node->in_use = 1;

            // node->prev_head->.....
            while (true) {
                Node* head = m_head.load(std::memory_order_acquire);
                if (head) {
                    auto res = try_push_element(head, node->data[0]);
                    if (res) {
                        m_alloc.delete_object(node);
                        return res;
                    }
                }

                // This node is not owned by anyone
                node->next.store(head, std::memory_order_relaxed);
                if (m_head.compare_exchange_weak(
                    head,
                    node,
                    std::memory_order_release,
                    std::memory_order_relaxed
                )) {
                    return { node, 0 };
                }
            }
        }

        auto insert_or_push(value_type val) noexcept(std::is_nothrow_move_assignable_v<value_type>) -> Index {
            auto root = m_head.load(std::memory_order_relaxed);
            while (root) {
                auto res = try_push_element(root, val);
                if (res) return res;
                root = root->next.load(std::memory_order_relaxed);
            }
            return push(std::move(val));
        }

        auto consume(auto&& fn) noexcept(std::is_nothrow_move_assignable_v<value_type>) -> bool {
            Node* head = m_head.exchange(nullptr, std::memory_order_acquire);
            if (!head) return false;
            auto root = head;
            while (root) {
                auto bits = root->in_use.load(std::memory_order_relaxed) & full;
                while (bits) {
                    auto set_bit = bits & -bits;
                    auto pos = static_cast<std::size_t>(std::countr_zero(set_bit));
                    fn(std::move(root->data[pos]));
                    bits ^= set_bit;
                }
                root = root->next.load(std::memory_order_relaxed);
            }
            destroy(head);
            return true;
        }

        auto index_of(value_type v) const noexcept(std::equality_comparable<value_type>) -> Index {
            Node* root = m_head.load(std::memory_order_acquire);
            while (root) {
                auto bits = root->in_use.load(std::memory_order_relaxed) & full;
                while (bits) {
                    auto set_bit = bits & -bits;
                    auto pos = static_cast<std::size_t>(std::countr_zero(set_bit));
                    if (root->data[pos] == v) return {
                        root,
                        pos
                    };
                    bits ^= set_bit;
                }
                root = root->next.load(std::memory_order_relaxed);
            }
            return {};
        }

        constexpr auto size() const noexcept -> std::size_t {
            Node* root = m_head.load(std::memory_order_relaxed);
            std::size_t count{};
            while (root) {
                count += static_cast<std::size_t>(std::popcount(root->in_use.load(std::memory_order_relaxed)));
                root = root->next.load(std::memory_order_relaxed);
            }
            return count;
        }
    private:
        void destroy(Node* root) {
            while (root) {
                auto next = root->next.exchange(nullptr, std::memory_order_relaxed);
                m_alloc.delete_object(root);
                root = next;
            }
        }

        constexpr auto try_push_element(Node* node, reference val) noexcept(std::is_nothrow_move_assignable_v<value_type>) -> Index {
            while (true) {
                auto bits = node->in_use.load(std::memory_order_acquire);
                // All the bits are set
                if (bits == full) return {};

                auto empty = (~bits & (bits + 1)) & full; // 0b101 -> 0b010
                auto pos = static_cast<std::size_t>(std::countr_zero(empty));
                if (node->in_use.fetch_or(bits | empty) != bits) continue;
                node->data[pos] = std::move(val);
                return { node, pos };
            }
        }
    private:
        std::atomic<Node*> m_head{};
        std::pmr::polymorphic_allocator<std::byte> m_alloc;
    };
} // namespace tpl

#endif // AMT_TPL_LINK_LIST_HPP
