#ifndef AMT_TPL_ALLOCATOR_HPP
#define AMT_TPL_ALLOCATOR_HPP

#include "tpl/atomic.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <utility>
#include "maths.hpp"

#ifdef TPL_ALLOCATOR_TRACK
    #include <mutex>
    #include <unordered_set>
    #include <print>
    #define TPL_ALLOCATOR_CONSTEXPR
#else
    #define TPL_ALLOCATOR_CONSTEXPR constexpr
#endif

namespace tpl {
    struct BumpAllocator {
        using size_type = std::size_t;
        using ref_t = atomic::Atomic::Int;

        TPL_ALLOCATOR_CONSTEXPR BumpAllocator() noexcept = default; 
        TPL_ALLOCATOR_CONSTEXPR BumpAllocator(BumpAllocator const&) noexcept = delete;
        TPL_ALLOCATOR_CONSTEXPR BumpAllocator(BumpAllocator && other) noexcept
            : m_mem(other.m_mem)
            , m_owned(other.m_owned)
        {
            m_mem = nullptr;
        }
        TPL_ALLOCATOR_CONSTEXPR BumpAllocator& operator=(BumpAllocator const&) noexcept = delete;
        TPL_ALLOCATOR_CONSTEXPR BumpAllocator& operator=(BumpAllocator && other) noexcept {
            if (this == &other) return *this;
            auto temp = BumpAllocator(std::move(other));
            swap(*this, temp);
            return *this;
        }
        ~BumpAllocator() noexcept {
            if (!m_owned || !m_mem) return;
            delete[] m_mem;
        }

        explicit TPL_ALLOCATOR_CONSTEXPR BumpAllocator(std::byte* buffer, size_type size_bytes) noexcept
            : m_mem(buffer)
            , m_size(size_bytes)
            , m_owned(false)
        {
            assert(buffer != nullptr);
        }

        explicit BumpAllocator(size_type size_bytes)
            : m_size(size_bytes)
            , m_owned(true)
        {
            m_mem = new std::byte[size_bytes]; 
            assert(m_mem != nullptr);
        }

        template <typename T>
        TPL_ATOMIC_FUNC_ATTR [[nodiscard]] auto alloc(size_type number_of_objects = 1, size_type alignment = alignof(T)) noexcept -> T* {
            auto const size_bytes = static_cast<size_type>(sizeof(T) * number_of_objects);
            while (true) {
                auto m = try_alloc(size_bytes, alignment);
                if (std::get<0>(m) == nullptr) return nullptr;
                if (m_ref.compare_exchange(std::get<2>(m), std::get<1>(m), std::memory_order_release, std::memory_order_relaxed)) {
                    auto ptr = std::get<0>(m);
                    return reinterpret_cast<T*>(ptr);
                }
            }
        }

        template <typename T>
        TPL_ATOMIC_FUNC_ATTR [[nodiscard]] auto realloc(
            T* ptr,
            size_type old_number_of_objects,
            size_type new_number_of_objects,
            size_type alignment = alignof(T)
        ) noexcept -> T* {
            while (true) {
                auto m = try_realloc(ptr, old_number_of_objects, new_number_of_objects, alignment);
                if (std::get<3>(m) == false){
                    return std::get<0>(m);
                }
                if (m_ref.compare_exchange(std::get<2>(m), std::get<1>(m), std::memory_order_release, std::memory_order_relaxed)) {
                    auto np = std::get<0>(m);
                    if (np == ptr) return ptr;
                    std::memcpy(np, ptr, std::min(old_number_of_objects, new_number_of_objects) * sizeof(T));
                    return np;
                }
            }
        }

        template <typename T>
        TPL_ATOMIC_FUNC_ATTR auto dealloc([[maybe_unused]] T* ptr) noexcept -> bool {
            ensure_no_double_free(ptr);
            track(ptr);
            bool freed = false;
            constexpr auto make_ref = [](ref_t old) -> ref_t {
                if (old.first <= 1) return {};
                return ref_t{ .first = old.first - 1, .second = old.second };
            };
            while (true) {
                auto r = m_ref.load(std::memory_order_acquire);
                ref_t nr = make_ref(r);
                if (m_ref.compare_exchange(r, nr, std::memory_order_release, std::memory_order_relaxed)) {
                    freed = true;
                    break;
                }
            }

            reset_tracking_if_needed();
            return freed;
        }

        constexpr auto free_space() const noexcept -> size_type {
            return size() - cursor();
        }

        constexpr auto is_owned() const noexcept -> bool {
            return m_owned;
        }

        constexpr auto size() const noexcept -> size_type {
            return m_size;
        }

        constexpr auto empty() const noexcept -> bool {
            return marker().first == 0;
        }

        TPL_ALLOCATOR_CONSTEXPR auto reset() noexcept -> void {
            set_marker({});
            #ifdef TPL_ALLOCATOR_TRACK
            auto scope = std::scoped_lock(track_mutex);
            freed_chunk.clear();
            #endif
        }

        constexpr auto marker() const noexcept -> ref_t {
            return m_ref.load(std::memory_order_acquire);
        }

        constexpr auto set_marker(ref_t m) noexcept -> void {
            m_ref.store(m, std::memory_order_relaxed);
        }

        constexpr auto cursor() const noexcept -> size_type {
            auto r = m_ref.load(std::memory_order_acquire);
            return r.second;
        }

        template <typename T>
        auto in_range(T const* const ptr) const noexcept -> bool {
            auto p = reinterpret_cast<std::byte const* const>(ptr);
            return p >= m_mem && p <= m_mem + m_size;
        }

        friend constexpr auto swap(BumpAllocator& lhs, BumpAllocator& rhs) noexcept -> void {
            using std::swap;
            swap(lhs.m_mem, rhs.m_mem);
            swap(lhs.m_owned, rhs.m_owned);
        }
    private:
        auto align_to(std::byte* addr, size_type alignment) const noexcept -> std::byte* {
            auto a = static_cast<std::intptr_t>(alignment);
            auto temp = (reinterpret_cast<std::intptr_t>(addr) + a - 1) & ~(a - 1);
            return reinterpret_cast<std::byte*>(temp);
        }

        TPL_ATOMIC_FUNC_ATTR [[nodiscard]] auto try_alloc(
            size_type size_bytes,
            size_type alignment
        ) const noexcept -> std::tuple<std::byte*, ref_t, ref_t> {
            assert(m_size != 0);
            assert(alignment > 0);
            assert(maths::is_power_of_two(alignment) && "alignment should be a power of 2");

            if (size_bytes > free_space()) return { nullptr, ref_t(), {} };

            auto r = m_ref.load(std::memory_order_acquire);
            auto start = r.second;

            auto base_ptr = align_to(m_mem + start, alignment);

            if (!in_range(base_ptr + size_bytes)) return { nullptr, ref_t(), r };
            auto offset = static_cast<unsigned>((base_ptr + size_bytes) - (m_mem + start));

            return { base_ptr, ref_t{ .first = r.first + 1, .second = r.second + offset }, r };
        }

        template <typename T>
        TPL_ATOMIC_FUNC_ATTR [[nodiscard]] auto try_realloc(
            T* ptr,
            size_type old_number_of_objects,
            size_type new_number_of_objects,
            size_type alignment = alignof(T)
        ) noexcept -> std::tuple<T*, ref_t, ref_t, bool> {
            if (old_number_of_objects == new_number_of_objects) return { ptr, {}, {}, false };

            auto pos = static_cast<decltype(ref_t::first)>(reinterpret_cast<std::byte*>(ptr + old_number_of_objects) - m_mem);
            ref_t r = m_ref.load(std::memory_order_acquire);
            if (pos == r.second) {
                if (!in_range(ptr + new_number_of_objects)) {
                    return { nullptr, {}, {}, false };
                }
                ref_t nr = r;
                if (old_number_of_objects > new_number_of_objects) {
                    auto offset = old_number_of_objects - new_number_of_objects;
                    nr.second = std::max(offset, nr.second) - offset;
                } else {
                    auto offset = new_number_of_objects - old_number_of_objects;
                    nr.second += offset;
                }
                return { ptr, nr, r, true };
            }
            if (new_number_of_objects < old_number_of_objects) return { ptr, r, r, false };
            ptr = alloc<T>(new_number_of_objects * sizeof(T), alignment); 
            return { ptr, r, r, false };
        }

        template <typename T>
        TPL_ALLOCATOR_CONSTEXPR auto track([[maybe_unused]] T* ptr) -> void {
            #ifdef TPL_ALLOCATOR_TRACK
            auto scope = std::scoped_lock(track_mutex);
            freed_chunk.insert(reinterpret_cast<std::byte*>(ptr));
            #endif
        }

        template <typename T>
        TPL_ALLOCATOR_CONSTEXPR auto untrack([[maybe_unused]] T* ptr) -> void {
            #ifdef TPL_ALLOCATOR_TRACK
            auto scope = std::scoped_lock(track_mutex);
            freed_chunk.erase(reinterpret_cast<std::byte*>(ptr));
            #endif
        }

        TPL_ALLOCATOR_CONSTEXPR auto reset_tracking_if_needed() -> void {
            #ifdef TPL_ALLOCATOR_TRACK
            auto scope = std::scoped_lock(track_mutex);
            if (marker().first != 0) return;
            freed_chunk.clear();
            #endif
        }

        template <typename T>
        TPL_ALLOCATOR_CONSTEXPR auto ensure_no_double_free([[maybe_unused]] T* ptr) {
            #ifdef TPL_ALLOCATOR_TRACK
            auto scope = std::scoped_lock(track_mutex);
            if (freed_chunk.contains(reinterpret_cast<std::byte*>(ptr))) {
                std::println(stderr, "Memory is already freed");
                abort();
            }
            #endif
        }
    private:
        std::byte* m_mem{nullptr};
        size_type m_size{};
        atomic::Atomic m_ref{};
        bool m_owned{false};
        #ifdef TPL_ALLOCATOR_TRACK
        std::mutex track_mutex;
        std::unordered_set<std::byte*> freed_chunk;
        #endif
    };

    struct BlockAllocator {
        using size_type = BumpAllocator::size_type;
        using block_type = std::vector<std::unique_ptr<BumpAllocator>>;
        static constexpr size_type default_size = 2 * 1024 * 1024; // 2MB;
    private:
        struct Node {
            BumpAllocator bm;
            alignas(atomic::internal::hardware_destructive_interference_size) std::atomic<Node*> next{nullptr};
        };
    public:

        BlockAllocator() = default;
        BlockAllocator(BlockAllocator const&) = delete;
        BlockAllocator(BlockAllocator &&) noexcept = delete;
        BlockAllocator& operator=(BlockAllocator const&) = delete;
        BlockAllocator& operator=(BlockAllocator &&) noexcept = delete;
        ~BlockAllocator() = default;

        BlockAllocator(BumpAllocator&& bm, std::string name) noexcept
            : m_name(std::move(name))
        {
            m_used.emplace_back(std::make_unique<BumpAllocator>(std::move(bm)));
        }

        BlockAllocator(std::string name) noexcept
            : m_name(std::move(name))
        {}

        template <typename T>
        TPL_ATOMIC_FUNC_ATTR [[nodiscard]] auto alloc(size_type number_of_objects = 1, size_type alignment = alignof(T)) noexcept -> T* {

            while (true) {
                {
                    auto ptr = try_alloc<T>(number_of_objects, alignment);
                    if (ptr) return ptr;
                }

                auto node = new Node{ .bm = BumpAllocator(std::max<size_type>(number_of_objects * sizeof(T) * 2, default_size)), .next = nullptr };
                auto ptr = node->bm.alloc<T>(number_of_objects, alignment);

                auto root = m_root.load(std::memory_order_acquire);
                node->next.store(root, std::memory_order_relaxed);
                if (m_root.compare_exchange_weak(root, node, std::memory_order_release, std::memory_order_relaxed)) {
                    return ptr;
                }
                delete node;
            }
            return nullptr;
        }

        template <typename T>
        TPL_ATOMIC_FUNC_ATTR [[nodiscard]] auto realloc(
            T* old_ptr,
            size_type old_number_of_objects,
            size_type new_number_of_objects,
            size_type alignment = alignof(T)
        ) noexcept -> T* {
            while (true) {
                {
                    auto ptr = try_realloc<T>(old_ptr, old_number_of_objects, new_number_of_objects, alignment);
                    if (ptr) return ptr;
                }

                auto node = new Node{ .bm = BumpAllocator(std::max<size_type>(new_number_of_objects * sizeof(T) * 2, default_size)), .next = nullptr };
                auto ptr = node->bm.alloc<T>(new_number_of_objects, alignment);

                auto root = m_root.load(std::memory_order_acquire);
                node->next.store(root, std::memory_order_relaxed);
                if (m_root.compare_exchange_weak(root, node, std::memory_order_release, std::memory_order_relaxed)) {
                    std::memcpy(ptr, old_ptr, std::min(old_number_of_objects, new_number_of_objects) * sizeof(T));
                    dealloc(old_ptr);
                    return ptr;
                }
                delete node;
            }

            return nullptr;
        }

        template <typename T>
        TPL_ATOMIC_FUNC_ATTR auto dealloc(T* ptr) noexcept -> bool {
            if (ptr == nullptr) return false;

            auto root = m_root.load(std::memory_order_relaxed);
            while (root) {
                if (root->bm.in_range(ptr)) {
                    return root->bm.dealloc(ptr);
                }
                root = root->next.load(std::memory_order_relaxed);
            }
            return false;
        }

        constexpr auto name() const noexcept -> std::string_view { return m_name; }
        constexpr auto nblocks() const noexcept -> std::size_t {
            auto sz = std::size_t{};
            auto root = m_root.load(std::memory_order_relaxed);
            while (root) {
                sz++;
                root = root->next.load(std::memory_order_relaxed);
            }
            return sz;
        }
        constexpr auto empty() const noexcept -> bool { return nblocks() == 0; }
        constexpr auto operator[](size_type k) noexcept -> BumpAllocator* {
            auto pos = size_type{};
            auto root = m_root.load(std::memory_order_relaxed);
            while (root) {
                if (pos++ == k) return &root->bm;
                root = root->next.load(std::memory_order_relaxed);
            }
            return nullptr;
        }
        constexpr auto operator[](size_type k) const noexcept -> BumpAllocator const* {
            auto self = const_cast<BlockAllocator*>(this);
            return self->operator[](k);
        }

        constexpr auto back() noexcept -> BumpAllocator* {
            auto root = m_root.load(std::memory_order_relaxed);
            while (root) {
                auto tmp = root;
                root = root->next.load(std::memory_order_relaxed);
                if (root == nullptr) return &tmp->bm;
            }
            return nullptr;
        }

        constexpr auto back() const noexcept -> BumpAllocator const* {
            auto self = const_cast<BlockAllocator*>(this);
            return self->back();
        }

        constexpr auto front() const noexcept -> BumpAllocator const* {
            return &m_root.load(std::memory_order_relaxed)->bm;
        }

        constexpr auto front() noexcept -> BumpAllocator* {
            return &m_root.load(std::memory_order_relaxed)->bm;
        }

        auto total_used() const noexcept -> std::size_t {
            auto sz = std::size_t{};
            auto root = m_root.load(std::memory_order_relaxed);
            while (root) {
                sz += root->bm.size();
                root = root->next.load(std::memory_order_relaxed);
            }
            return sz;
        }
        auto total_objects() const noexcept -> std::size_t {
            auto sz = std::size_t{};
            auto root = m_root.load(std::memory_order_relaxed);
            while (root) {
                sz += root->bm.marker().first;
                root = root->next.load(std::memory_order_relaxed);
            }
            return sz;
        }

        struct Marker {
            Node* alloc;
            BumpAllocator::ref_t ref;
        };

        auto marker() const noexcept -> Marker {
            auto root = m_root.load(std::memory_order_acquire);
            if (!root) return {};

            auto m = Marker{
                .alloc = root,
                .ref = root->bm.marker()
            };
            return m;
        }

        auto set_marker(Marker m) noexcept -> void {
            m.alloc->bm.set_marker(m.ref);
            auto root = m_root.exchange(m.alloc);
            while (root) {
                if (root == m.alloc) break;
                auto tmp = root;
                root = root->next.load(std::memory_order_relaxed);
                delete tmp;
            }
        }

        auto reset(bool reuse = true) noexcept {
            Node* root{};
            if (!reuse) root = m_root.exchange(nullptr);
            else root = m_root.load(std::memory_order_acquire);
            while (root) {
                auto tmp = root;
                root = root->next.load(std::memory_order_acquire);
                if (!reuse) delete tmp;
                else tmp->bm.reset();
            }
        }

    private:
        template <typename T>
        [[nodiscard]] auto try_realloc(
            T* old_ptr,
            size_type old_number_of_objects,
            size_type new_number_of_objects,
            size_type alignment = alignof(T)
        ) noexcept -> T* {
            auto root = m_root.load(std::memory_order_relaxed);
            while (root) {
                auto ptr = root->bm.realloc<T>(old_ptr, old_number_of_objects, new_number_of_objects, alignment);
                if (ptr) return ptr;
                root = root->next.load(std::memory_order_relaxed);
            }
            return nullptr;
        }

        template <typename T>
        [[nodiscard]] auto try_alloc(size_type number_of_objects = 1, size_type alignment = alignof(T)) noexcept -> T* {
            auto root = m_root.load(std::memory_order_relaxed);
            while (root) {
                auto ptr = root->bm.alloc<T>(number_of_objects, alignment);
                if (ptr) return ptr;
                root = root->next.load(std::memory_order_relaxed);
            }
            return nullptr;
        }
    private:
        alignas(atomic::internal::hardware_destructive_interference_size) std::atomic<Node*> m_root{};
        block_type m_used;
        std::string m_name;
    };

    struct AllocatorManager {
        static AllocatorManager& instance() noexcept {
            static AllocatorManager tmp;
            return tmp;
        }

        constexpr auto swap(BlockAllocator* a) noexcept {
            m_current.store(a, std::memory_order_release);
        }

        constexpr auto reset() noexcept -> void {
            swap(&m_global);
        }

        constexpr auto get_alloc() const noexcept -> BlockAllocator* {
            return m_current.load(std::memory_order_relaxed);    
        }

        constexpr static auto get_global_alloc() noexcept -> BlockAllocator* {
            return &m_global;
        }

        constexpr auto is_global_alloc() const noexcept -> bool {
            return get_alloc() == get_global_alloc();
        }
    private:
        constexpr AllocatorManager() noexcept = default;
    private:
        static BlockAllocator m_global;
        std::atomic<BlockAllocator*> m_current{&m_global};
    };

    inline BlockAllocator AllocatorManager::m_global = BlockAllocator("Global Allocator");

} // namespace tpl

#endif // AMT_TPL_ALLOCATOR_HPP
