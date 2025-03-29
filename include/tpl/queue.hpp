#ifndef AMT_TPL_QUEUE_HPP
#define AMT_TPL_QUEUE_HPP

#include <atomic>
#include <print>
#include <utility>

#include "allocator.hpp"
#include "basic.hpp"
#include "atomic.hpp"
#include <cstdint>
#include <new>
#include <type_traits>

namespace tpl {

    namespace internal {
        using atomic::internal::hardware_destructive_interference_size;

        template <typename T, std::size_t N>
        struct StaticCircularArray {
            using size_type = std::size_t;
            T data[N];
            static_assert(N > 0 && maths::is_power_of_two(N), "must be a power of 2");

            constexpr auto operator[](size_type k) const noexcept -> T { return data[k & (N - 1)]; }
            constexpr auto operator[](size_type k) noexcept -> T& { return data[k & (N - 1)]; }
            constexpr auto size() const noexcept -> size_type { return N; }
            constexpr auto index_mask() const noexcept -> size_type { return N - 1; }
            constexpr auto capacity() const noexcept -> size_type { return N; }
        };

        template<typename ValueT, typename IndexT>
        struct alignas(sizeof(atomic::Atomic)) QueueEntry: atomic::Atomic {
            using value_t = ValueT;
            using index_t = IndexT;

            static_assert(sizeof(ValueT) <= sizeof(int_t), "cannot store value bigger than max pointer size");
            static_assert(sizeof(IndexT) <= sizeof(int_t), "cannot store index bigger than max pointer size");

            constexpr QueueEntry() noexcept = default;
            constexpr QueueEntry(QueueEntry const&) noexcept = default;
            constexpr QueueEntry(QueueEntry &&) noexcept = default;
            constexpr QueueEntry& operator=(QueueEntry const&) noexcept = default;
            constexpr QueueEntry& operator=(QueueEntry &&) noexcept = default;
            constexpr ~QueueEntry() noexcept = default;
            constexpr QueueEntry(index_t index, value_t val = value_t{}) noexcept
                : Atomic(static_cast<int_t>(index), static_cast<int_t>(val))
            {}
            constexpr QueueEntry(index_t index, value_t val = value_t{}) noexcept requires (std::is_pointer_v<value_t>)
                : Atomic(static_cast<int_t>(index), reinterpret_cast<int_t>(val))
            {}

            constexpr auto clear() noexcept -> void {
                data = {};
            }

            constexpr auto set_seq(index_t index) noexcept -> void {
                data = Int { index, 0 };
            }

            constexpr auto set(index_t index, value_t val) noexcept -> void {
                if constexpr (std::is_pointer_v<value_t>) {
                    data = { static_cast<int_t>(index), reinterpret_cast<int_t>(val) };
                } else {
                    data = { static_cast<int_t>(index), static_cast<int_t>(val) };
                }
            }

            constexpr auto get_seq() const noexcept -> index_t {
                return static_cast<index_t>(data.first);
            }

            constexpr auto get_value() const noexcept -> value_t {
                if constexpr (std::is_pointer_v<value_t>) {
                    return reinterpret_cast<value_t>(data.second);
                } else {
                    return static_cast<value_t>(data.second);
                }
            }

            constexpr auto is_empty() const noexcept -> bool {
                return !(get_seq() & 1);
            }

            constexpr auto is_full() const noexcept -> bool {
                return !is_empty();
            }

            TPL_ATOMIC_FUNC_ATTR auto load(std::memory_order order = std::memory_order_seq_cst) noexcept -> QueueEntry {
                return std::bit_cast<QueueEntry>(Atomic::load(order));
            }

            TPL_ATOMIC_FUNC_ATTR auto store(std::memory_order order = std::memory_order_seq_cst) noexcept -> QueueEntry {
                return std::bit_cast<QueueEntry>(Atomic::store(order));
            }

            TPL_ATOMIC_FUNC_ATTR auto compare_exchange(
                QueueEntry expected,
                QueueEntry new_value,
                std::memory_order success = std::memory_order_seq_cst,
                std::memory_order failure = std::memory_order_seq_cst
            ) noexcept -> bool {
                return Atomic::compare_exchange(expected.data, new_value.data, success, failure);
            }
        };

        template<typename T, unsigned N>
        struct alignas(N) aligned_type : public T {};

        // https://github.com/erez-strauss/lockfree_mpmc_queue/blob/master/mpmc_queue.h
        template <typename T, unsigned N, typename Allocator = void>
            requires (maths::is_non_zero_power_of_two(N))
        struct CircularQueue {
            using int_t = atomic::Atomic::int_t;
            static constexpr bool is_small = sizeof(T) <= sizeof(int_t);
            using index_t = std::uint32_t;
            using value_t = std::conditional_t<
                is_small,
                int_t,
                T*
            >;
            using entry_t = QueueEntry<value_t, index_t>;
            using base_type = StaticCircularArray<aligned_type<entry_t, hardware_destructive_interference_size>, N>;
            using size_type = typename base_type::size_type;

            constexpr CircularQueue() noexcept requires (std::is_void_v<Allocator>)
                : m_allocator(nullptr)
            {
                for (auto i = 0u; i < m_data.size(); ++i) m_data[i].set_seq(index_t(i << 1));
            }
            constexpr CircularQueue(CircularQueue const&) noexcept = delete;
            constexpr CircularQueue(CircularQueue &&) noexcept = delete;
            constexpr CircularQueue& operator=(CircularQueue const&) noexcept = delete;
            constexpr CircularQueue& operator=(CircularQueue &&) noexcept = delete;
            ~CircularQueue() noexcept {
                clear();
            }

            constexpr CircularQueue(BlockAllocator* alloc) noexcept requires (!std::is_void_v<Allocator>)
                : m_allocator(alloc)
            {
                for (auto i = 0u; i < m_data.size(); ++i) m_data[i].set_seq(index_t(i << 1));
            }

            constexpr auto size() const noexcept -> size_type {
                if (empty()) return 0;
                auto w = m_write_index.load(std::memory_order_acquire);
                auto r = m_read_index.load(std::memory_order_acquire);
                if (w > r) return w - r;
                return w + m_data.size() - r;
            }

            constexpr auto empty() const noexcept -> bool {
                auto r = m_read_index.load(std::memory_order_acquire);
                auto entry = entry_t(m_data[r].load(std::memory_order_acquire));
                if (entry.get_seq() == index_t{r << 1}) return true;
                return false;
            }

            constexpr auto full() const noexcept -> bool {
                return size() == N;
            }

            constexpr auto clear() noexcept(std::is_nothrow_destructible_v<T>) -> void {
                while (pop()) {};
                for (auto i = 0u; i < m_data.size(); ++i) m_data[i].set_seq(index_t(i << 1));
                m_read_index.store(0);
                m_write_index.store(0);
            }

            TPL_ATOMIC_FUNC_ATTR auto pop() noexcept(std::is_nothrow_move_assignable_v<T>) -> std::optional<T> {
                while (true) {
                    auto r = m_read_index.load(std::memory_order_relaxed);
                    entry_t data_entry = m_data[r].load(std::memory_order_relaxed);
                    auto sq = data_entry.get_seq();
                    index_t empty_idx = r << 1;
                    index_t full_idx = empty_idx | 1;
                    index_t old_idx = static_cast<index_t>((r + N) << 1);

                    if (sq == full_idx) {
                        auto empty_entry = entry_t(old_idx, 0);
                        if (m_data[r].compare_exchange(
                            data_entry,
                            empty_entry,
                            std::memory_order_release,
                            std::memory_order_relaxed
                            )
                        ) {
                            m_read_index.compare_exchange_strong(
                                r,
                                r + 1,
                                std::memory_order_release,
                                std::memory_order_relaxed
                            );
                            value_t ptr = data_entry.get_value();
                            if constexpr (!is_small) {
                                auto val = std::move(*ptr);
                                m_allocator->dealloc(ptr);
                                return std::move(val);
                            } else {
                                auto item = static_cast<typename internal::storage_value<sizeof(T)>::type>(ptr);
                                return { std::move(std::bit_cast<T>(item)) };
                            }
                        }
                    } else if ((sq | 1) == (old_idx | 1)) {
                        m_read_index.compare_exchange_strong(
                            r,
                            r + 1,
                            std::memory_order_release,
                            std::memory_order_relaxed
                        );
                    } else if (sq == empty_idx) {
                        return {};
                    }
                }
            }

            template <typename... Args>
            TPL_ATOMIC_FUNC_ATTR auto emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) -> bool requires (!std::is_void_v<Allocator>) {
                if constexpr (!is_small) {
                    auto mem = m_allocator->template alloc<T>();
                    new (mem) T(std::forward<Args>(args)...);
                    return push_value(mem);
                } else {
                    return push(T(std::forward<Args>(args)...));
                }
            }

            TPL_ATOMIC_FUNC_ATTR auto push(T val) noexcept(std::is_nothrow_move_constructible_v<T>) -> bool requires (std::is_move_constructible_v<T> && !std::is_void_v<Allocator>) {
                if constexpr (!is_small) {
                    auto mem = m_allocator->template alloc<T>();
                    new (mem) T(std::move(val));
                    return push_value(mem);
                } else {
                    auto item = std::bit_cast<typename internal::storage_value<sizeof(T)>::type>(val);
                    return push_value(static_cast<int_t>(item));
                }
            }

            TPL_ATOMIC_FUNC_ATTR auto push_value(T* val) noexcept -> bool requires (!std::same_as<T, value_t> && is_small) {
                union {
                    T* o;
                    int_t n;
                } tmp;
                tmp.o = val;
                return push_value(tmp.n);
            }

            TPL_ATOMIC_FUNC_ATTR auto push_value(value_t val) noexcept -> bool {
                while (true) {
                    index_t w = m_write_index.load(std::memory_order_relaxed);

                    index_t sq = m_data[w].get_seq();
                    index_t empty_idx = w << 1;
                    index_t full_idx = empty_idx | 1;
                    index_t old_idx = static_cast<index_t>((w + N) << 1);

                    if (sq == empty_idx) {
                        auto empty_data = entry_t(empty_idx, 0);
                        auto data_entry = entry_t(full_idx, val);

                        if (m_data[w].compare_exchange(
                            empty_data,
                            data_entry,
                            std::memory_order_release,
                            std::memory_order_relaxed
                            )
                        ) {
                            m_write_index.compare_exchange_strong(
                                w, w + 1,
                                std::memory_order_release,
                                std::memory_order_relaxed
                            );
                            return true;
                        }
                    } else if ((sq == full_idx) || (sq == old_idx)) {
                        m_write_index.compare_exchange_strong(
                            w, w + 1,
                            std::memory_order_release,
                            std::memory_order_relaxed
                        );
                    } else if (full_idx == static_cast<index_t>(sq + (N << 1))) {
                        return false;
                    }
                }
            }

        private:
            base_type m_data;
            Allocator* m_allocator{nullptr};
            alignas(hardware_destructive_interference_size) std::atomic<index_t> m_write_index{};
            alignas(hardware_destructive_interference_size) std::atomic<index_t> m_read_index{};
        };
    } // namespace internal

    template <typename T, unsigned N, typename Allocator = void>
    using BoundedQueue = internal::CircularQueue<T, N, Allocator>;

    template <typename T, unsigned BlockSize = 128>
    struct Queue {
        static constexpr auto block_size = BlockSize;
        using size_type = std::size_t;
        using pointer = T*;
        using const_pointer = T const*;
    private:
        using node_inner_t = BoundedQueue<atomic::Atomic::int_t, BlockSize, BlockAllocator>;
        struct Node {
            node_inner_t q;
            alignas(internal::hardware_destructive_interference_size) std::atomic<Node*> next{};

            constexpr Node(BlockAllocator* alloc) noexcept
                : q(alloc)
            {}
        };
    public:
        constexpr Queue() noexcept = default;
        constexpr Queue(Queue const&) noexcept = delete;
        constexpr Queue(Queue &&) noexcept = default;
        constexpr Queue& operator=(Queue const&) noexcept = delete;
        constexpr Queue& operator=(Queue &&) noexcept = default;
        ~Queue() noexcept {
            std::println("{} == {} + {}", m_node_allocator.total_objects(), nodes(), m_free_nodes.size());
            assert(m_node_allocator.total_objects() == nodes() + m_free_nodes.size());
            {
                Node* tmp = m_tail.load();
                while (tmp) {
                    tmp->q.clear();
                    auto next = tmp->next.load();
                    tmp = next;
                }
            }
            m_node_allocator.reset();
        }

        constexpr auto size() const noexcept -> size_type {
            size_type count{};
            auto tail = m_tail.load(std::memory_order_relaxed);
            while (tail) {
                count += tail->q.size();
                tail = tail->next.load(std::memory_order_relaxed);
            }
            return count;
        }

        constexpr auto nodes() const noexcept -> size_type {
            size_type count{};
            auto tail = m_tail.load(std::memory_order_relaxed);
            while (tail) {
                ++count;
                tail = tail->next.load(std::memory_order_relaxed);
            }
            return count;
        }

        constexpr auto empty() const noexcept -> bool {
            return size() == 0;
        }

        template <typename... Args>
        TPL_ATOMIC_FUNC_ATTR auto emplace(Args&&... args) -> bool {
            return push(T(std::forward<Args>(args)...));
        }

        TPL_ATOMIC_FUNC_ATTR auto push(T val) -> bool requires (!std::same_as<T, typename node_inner_t::value_t>) {
            if constexpr (sizeof(T) <= sizeof(atomic::Atomic::int_t)) {
                auto item = std::bit_cast<typename internal::storage_value<sizeof(T)>::type>(val);
                return push(static_cast<node_inner_t::value_t>(item));
            } else {
                T* tmp = m_data_allocator.alloc<T>();
                if (!tmp) return false;
                new (tmp) T(std::move(val));
                if (!push(reinterpret_cast<node_inner_t::value_t>(tmp))) {
                    tmp->~T();
                    m_data_allocator.dealloc(tmp);
                    return false;
                }
                return true;
            }
        }

        TPL_ATOMIC_FUNC_ATTR auto push(node_inner_t::value_t val) -> bool {
            Node* head{};
            Node* node{};

            while (true) {
                head = m_head.load(std::memory_order_acquire);

                if (head != nullptr) {
                    if (head->q.push_value(val)) break;
                }

                if (!node) {
                    node = m_free_nodes.pop().value_or(nullptr);

                    if (!node) {
                        node = m_node_allocator.alloc<Node>();
                        new(node) Node(&m_data_allocator);
                    }
                }

                if (!node) return false;

                if (!m_head.compare_exchange_weak(head, node, std::memory_order_release, std::memory_order_relaxed)) {
                    continue;
                }

                if (head) head->next.store(node, std::memory_order_relaxed);
            }

            while (true) {
                Node* tail = m_tail.load(std::memory_order_acquire);
                if (tail != nullptr)  break;
                if (m_tail.compare_exchange_weak(tail, head, std::memory_order_release, std::memory_order_relaxed)) {
                    break;
                }
            }

            return true;
        }

        auto pop() -> std::optional<T> {
            Node* tail{};
            while (true) {
                tail = m_tail.load(std::memory_order_acquire);
                if (!tail) return {};

                auto tmp = tail->q.pop();

                if (tmp) {
                    auto node_value = *tmp;
                    if constexpr (sizeof(T) <= sizeof(atomic::Atomic::int_t)) {
                        auto item = static_cast<typename internal::storage_value<sizeof(T)>::type>(node_value);
                        return std::move(std::bit_cast<T>(item));
                    } else {
                        auto node = std::bit_cast<T*>(node_value);
                        auto val = std::move(*node);
                        node->~T();
                        m_data_allocator.dealloc(node);
                        return std::move(val);
                    }
                }

                Node* next = tail->next.load(std::memory_order_acquire);
                if (next == nullptr) {
                    return {};
                }

                if (m_tail.compare_exchange_weak(tail, next, std::memory_order_acquire, std::memory_order_relaxed)) {
                    push_back(tail);
                }

            }
            return {};
        }

    private:
        static constexpr auto node_scope(std::atomic<Node*> const& n, std::memory_order order, auto&& fn) noexcept -> void {
            Node* tmp = n.load(order);
            if (!tmp) return;
            fn(tmp);
        }

        constexpr auto push_back(Node* node) noexcept -> void {
            if (!node) return;
            node->next.store(nullptr, std::memory_order_relaxed);

            while (!m_free_nodes.emplace(node)) {
                Node* tmp = m_free_nodes.pop().value_or(nullptr);
                if (!tmp) continue;

                m_node_allocator.dealloc(tmp);
            }
        }
        using free_queue_t = BoundedQueue<Node*, BlockSize, BlockAllocator>;
    private:
        alignas(internal::hardware_destructive_interference_size) std::atomic<Node*> m_head{};
        alignas(internal::hardware_destructive_interference_size) std::atomic<Node*> m_tail{};
        BlockAllocator m_data_allocator;
        BlockAllocator m_node_allocator;
        free_queue_t m_free_nodes{&m_node_allocator};
    };
} // namespace tpl

#endif // AMT_TPL_QUEUE_HPP
