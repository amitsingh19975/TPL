#ifndef AMT_TPL_QUEUE_HPP
#define AMT_TPL_QUEUE_HPP

#include <atomic>
#include <cstdint>
#include <memory_resource>
#include <print>
#include <utility>
#include "basic.hpp"
#include "maths.hpp"
#include "atomic.hpp"
#include "hazard_ptr.hpp"
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
        template <typename T, unsigned N>
            requires (maths::is_non_zero_power_of_two(N) && (sizeof(T) <= sizeof(atomic::Atomic::int_t)))
        struct CircularQueue {
            using value_type = T;
            using int_t = atomic::Atomic::int_t;
            using index_t = std::uint32_t;
            using value_t = int_t;
            using entry_t = QueueEntry<value_t, index_t>;
            using base_type = StaticCircularArray<aligned_type<entry_t, hardware_destructive_interference_size>, N>;
            using size_type = typename base_type::size_type;

            constexpr CircularQueue() noexcept {
                for (auto i = 0u; i < m_data.size(); ++i) m_data[i].set_seq(index_t(i << 1));
            }
            constexpr CircularQueue(CircularQueue const&) noexcept = delete;
            constexpr CircularQueue(CircularQueue &&) noexcept = delete;
            constexpr CircularQueue& operator=(CircularQueue const&) noexcept = delete;
            constexpr CircularQueue& operator=(CircularQueue &&) noexcept = delete;
            ~CircularQueue() noexcept {
                clear();
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

            constexpr auto reset() noexcept -> void {
                for (auto i = 0u; i < m_data.size(); ++i) m_data[i].set_seq(index_t(i << 1));
                m_read_index.store(0);
                m_write_index.store(0);
            }

            constexpr auto clear() noexcept(std::is_nothrow_destructible_v<T>) -> void {
                while (pop()) {};
                reset();
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
                            auto item = static_cast<typename internal::storage_value<sizeof(T)>::type>(ptr);
                            return { std::move(std::bit_cast<T>(item)) };
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
            TPL_ATOMIC_FUNC_ATTR auto emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) -> bool {
                return push(T(std::forward<Args>(args)...));
            }

            TPL_ATOMIC_FUNC_ATTR auto push(T val) noexcept(std::is_nothrow_move_constructible_v<T>) -> bool requires (std::is_move_constructible_v<T>) {
                auto item = std::bit_cast<typename internal::storage_value<sizeof(T)>::type>(val);
                return push_value(static_cast<value_t>(item));
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
            alignas(hardware_destructive_interference_size) std::atomic<index_t> m_write_index{};
            alignas(hardware_destructive_interference_size) std::atomic<index_t> m_read_index{};
        };

        template <typename T>
        struct is_bounded_queue: std::false_type{};

        template <typename T, unsigned N>
        struct is_bounded_queue<CircularQueue<T, N>>: std::true_type{};

        template <typename T>
        static constexpr auto is_bounded_queue_v = is_bounded_queue<std::decay_t<T>>::value;
    } // namespace internal

    template <typename T, unsigned N>
    using BoundedQueue = internal::CircularQueue<T, N>;

    template <typename T, unsigned BlockSize = 128>
        requires (sizeof(T) <= sizeof(atomic::Atomic::int_t))
    struct Queue {
        static constexpr auto block_size = BlockSize;
        using value_type = T;
        using size_type = std::size_t;
        using pointer = T*;
        using const_pointer = T const*;
    private:
        using node_inner_t = BoundedQueue<atomic::Atomic::int_t, BlockSize>;
        struct Node: HazardPointerObjBase<Node> {
            node_inner_t q;
            std::atomic<Node*> next{};
        };
    public:
        constexpr Queue() noexcept = default;
        constexpr Queue(Queue const&) noexcept = delete;
        constexpr Queue(Queue &&) noexcept = default;
        constexpr Queue& operator=(Queue const&) noexcept = delete;
        constexpr Queue& operator=(Queue &&) noexcept = default;
        ~Queue() noexcept {
            reset();
        }

        constexpr Queue(std::pmr::polymorphic_allocator<std::byte> allocator) noexcept
            : m_alloc(std::move(allocator))
        {}

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

        auto reset() noexcept {
            set_queue_state(QUEUE_STATE_RESET);
            assert (!is_set_queue_state(static_cast<QueueState>(QUEUE_STATE_PUSH | QUEUE_STATE_POP)));
            {
                m_head.exchange(nullptr);
                Node* tmp = m_tail.exchange(nullptr);
                while (tmp) {
                    tmp->q.clear();
                    auto next = tmp->next.load(std::memory_order_relaxed);
                    retire_node(tmp);
                    tmp = next;
                }

                while (!m_free_nodes.empty()) {
                    tmp = m_free_nodes.pop().value_or(nullptr);
                    if (!tmp) break;
                    retire_node(tmp);
                }
            }
            while (m_domain.cleanup());
            clear_queue_state(QUEUE_STATE_RESET);
        }

        template <typename... Args>
        TPL_ATOMIC_FUNC_ATTR auto emplace(Args&&... args) -> bool {
            return push(T(std::forward<Args>(args)...));
        }

        TPL_ATOMIC_FUNC_ATTR auto push(T val) -> bool requires (!std::same_as<T, typename node_inner_t::value_t>) {
            auto item = std::bit_cast<typename internal::storage_value<sizeof(T)>::type>(val);
            return push(static_cast<node_inner_t::value_t>(item));
        }

        TPL_ATOMIC_FUNC_ATTR auto push(node_inner_t::value_t val) -> bool {
            assert(!is_set_queue_state(QUEUE_STATE_RESET));
            set_queue_state(QUEUE_STATE_PUSH);
            auto res = [this, val] -> bool {
                Node* node{};
                bool is_inserted{false};

                auto node_holder = make_hazard_pointer(m_domain);
                while (true) {
                    auto holder = make_hazard_pointer(m_domain);
                    auto head = holder.protect(m_head);

                    if (head != nullptr) {
                        if (is_inserted) break;
                        if (head->q.push_value(val)) {
                            break;
                        }
                    }

                    if (!node) {
                        node = m_free_nodes.pop().value_or(nullptr);
                        if (!node) {
                            node = m_alloc.new_object<Node>();
                        } else {
                            node->q.reset();
                            node->next = nullptr;
                        }
                        node->q.push_value(val);
                        node_holder.reset_protection(node);
                    }

                    if (!node) return false;

                    if (!m_head.compare_exchange_weak(
                        head,
                        node,
                        std::memory_order_release,
                        std::memory_order_relaxed
                    )) {
                        continue;
                    }
                    is_inserted = true;

                    // (old head) -> node(new head)
                    if (head) {
                        head->next.store(node, std::memory_order_relaxed);
                    } else {
                        m_tail.store(node);
                        return true;
                    }
                }

                node_holder.~HazardPointer();
                if (!is_inserted) {
                    push_back(node);
                }
                return true;
            }();
            clear_queue_state(QUEUE_STATE_PUSH);
            return res;
        }

        auto pop() -> std::optional<T> {
            assert(!is_set_queue_state(QUEUE_STATE_RESET));
            set_queue_state(QUEUE_STATE_POP);
            auto res = [this] -> std::optional<T> {
                while (true) {
                    auto holder = make_hazard_pointer(m_domain);
                    auto tail = holder.protect(m_tail);
                    if (!tail) return {};

                    auto tmp = tail->q.pop();

                    if (tmp) {
                        auto node_value = *tmp;
                        auto item = static_cast<typename internal::storage_value<sizeof(T)>::type>(node_value);
                        return std::bit_cast<T>(item);
                    }

                    Node* next = tail->next.load(std::memory_order_acquire);
                    if (next == nullptr) {
                        return {};
                    }

                    if (m_tail.compare_exchange_weak(
                        tail,
                        next,
                        std::memory_order_acquire,
                        std::memory_order_relaxed
                    )) {
                        holder.~HazardPointer();
                        // Keep the nodes around
                        push_back(tail);
                    }

                }
                return {};
            }();
            clear_queue_state(QUEUE_STATE_POP);
            return res;
        }

        constexpr auto full() const noexcept { return false; }

    private:
        constexpr auto retire_node(Node* node) noexcept -> void {
            node->retire([alloc = m_alloc] (Node* n) mutable {
                assert(reinterpret_cast<std::uintptr_t>(n) > 0x0FFF);
                alloc.delete_object(n);
            }, m_domain);
        }
        constexpr auto push_back(Node* node) noexcept -> void {
            if (!node) return;

            while (!m_free_nodes.emplace(node)) {
                Node* tmp = m_free_nodes.pop().value_or(nullptr);
                if (!tmp) continue;
                retire_node(tmp);
            }
        }
        using free_queue_t = BoundedQueue<Node*, BlockSize>;
        enum QueueState: std::uint8_t {
            QUEUE_STATE_PUSH    = 1,
            QUEUE_STATE_POP     = 2,
            QUEUE_STATE_RESET   = 4,
        };

        constexpr auto set_queue_state([[maybe_unused]] QueueState state) noexcept -> void {
            #ifndef NDEBUG
            m_queue_state.fetch_or(state);
            #endif
        }

        constexpr auto clear_queue_state([[maybe_unused]] QueueState state) noexcept -> void {
            #ifndef NDEBUG
            m_queue_state.fetch_and(static_cast<std::uint8_t>(~state));
            #endif
        }

        constexpr auto is_set_queue_state([[maybe_unused]] QueueState state) const noexcept -> bool {
            #ifndef NDEBUG
            return (m_queue_state.load(std::memory_order_relaxed) & state) == state;
            #else
            return false;
            #endif
        }
    private:
        alignas(internal::hardware_destructive_interference_size) std::atomic<Node*> m_head{};
        alignas(internal::hardware_destructive_interference_size) std::atomic<Node*> m_tail{};
        std::pmr::polymorphic_allocator<std::byte> m_alloc;
        HazardPointerDomain m_domain;
        free_queue_t m_free_nodes;
        #ifndef NDEBUG
        std::atomic<std::uint8_t> m_queue_state;
        #endif
    };
} // namespace tpl

#endif // AMT_TPL_QUEUE_HPP
