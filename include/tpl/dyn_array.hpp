#ifndef AMT_TPL_DYNAMIC_ARRAY_HPP
#define AMT_TPL_DYNAMIC_ARRAY_HPP

#include "allocator.hpp"
#include <algorithm>
#include <cassert>
#include <compare>
#include <concepts>
#include <initializer_list>
#include <iterator>
#include <new>
#include <numeric>
#include <span>
#include <type_traits>
#include <utility>

namespace tpl {

    template <typename T>
    struct DynArray {
        using value_type = T;
        using size_type = std::size_t;
        using reference = T&;
        using const_reference = T const&;
        using pointer = T*;
        using const_pointer = T const*;

        struct Iterator {
            using iterator_category = std::random_access_iterator_tag;
            using value_type        = T;
            using difference_type   = std::ptrdiff_t;
            using pointer           = T*;
            using reference         = T&;

            constexpr Iterator(pointer data) noexcept
                : m_data(data)
            {}
            constexpr Iterator(Iterator const&) noexcept = default;
            constexpr Iterator(Iterator &&) noexcept = default;
            constexpr Iterator& operator=(Iterator const&) noexcept = default;
            constexpr Iterator& operator=(Iterator &&) noexcept = default;
            constexpr ~Iterator() noexcept = default;

            constexpr reference operator*() const noexcept { return *m_data; }
            constexpr pointer operator->() const  noexcept{ return m_data; }

            constexpr Iterator& operator++() noexcept {
                ++m_data;
                return *this;
            }

            constexpr Iterator& operator++(int) noexcept {
                auto tmp = *this;
                ++m_data;
                return tmp;
            }

            constexpr Iterator& operator--() noexcept {
                --m_data;
                return *this;
            }

            constexpr Iterator& operator--(int) noexcept {
                auto tmp = *this;
                --m_data;
                return tmp;
            }

            constexpr Iterator operator+(difference_type n) const noexcept {
                return Iterator(m_data + n); 
            }

            constexpr Iterator& operator+=(difference_type n) noexcept {
                m_data += n; 
                return *this; 
            }

            constexpr Iterator operator-(difference_type n) const noexcept {
                return Iterator(m_data - n); 
            }

            constexpr Iterator& operator-=(difference_type n) noexcept {
                m_data -= n; 
                return *this; 
            }

            constexpr difference_type operator-(const Iterator& other) const noexcept { 
                return m_data - other.m_data; 
            }

            constexpr reference operator[](size_type k) const noexcept { return m_data[k]; }
            constexpr const_reference operator[](size_type k) noexcept { return m_data[k]; }

            constexpr auto operator<=>(Iterator const&) const noexcept -> std::strong_ordering = default;
            constexpr auto operator==(Iterator const&) const noexcept -> bool = default;
            constexpr auto operator!=(Iterator const&) const noexcept -> bool = default;
        private:
            pointer m_data{};
        };

        using iterator = Iterator;
        using const_iterator = Iterator const;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<iterator> const;

        constexpr DynArray() noexcept = default;
        DynArray(DynArray const& other) noexcept
            : m_size(other.m_size)
            , m_capacity(other.m_capacity)
            , m_alloc(other.m_alloc)
        {
            m_data = m_alloc->alloc<T>(other.m_size);
            std::copy_n(other.m_data, m_size, m_data);
        }
        constexpr DynArray(DynArray && other) noexcept
            : m_data(other.m_data)
            , m_size(other.m_size)
            , m_capacity(other.m_capacity)
            , m_alloc(other.m_alloc)
        {
            other.m_data = nullptr;
        }

        DynArray& operator=(DynArray const& other) noexcept {
            if (this == &other) return *this;
            m_data = m_alloc->alloc<T>(other.m_size);
            m_size = other.m_size;
            m_capacity = m_size;
            std::copy_n(other.m_data, m_size, m_data);
            return *this;
        }
        DynArray& operator=(DynArray && other) noexcept {
            if (this == &other) return *this;
            if (other.m_alloc == m_alloc) {
                swap(other, *this);
                return *this;
            }
            *this = other;
            return *this;
        }
        ~DynArray() noexcept {
            if (!m_data) return;
            for (auto i = 0zu; i < size(); ++i) m_data[i].~T();
            m_alloc->dealloc(m_data);
        }

        DynArray(std::initializer_list<T> li) noexcept
            : m_size(li.size())
            , m_capacity(li.size())
        {
            m_data = m_alloc->alloc<T>(m_size);
            std::copy(li.begin(), li.end(), m_data);
        }

        template <std::forward_iterator I>
        DynArray(I b, I e) noexcept
            : m_size(static_cast<size_type>(std::distance(b, e)))
            , m_capacity(m_size)
        {
            m_data = m_alloc->alloc<T>(m_size);
            std::copy(b, e, begin());
        }

        DynArray(size_type n) noexcept
            : m_size(n)
            , m_capacity(n)
        {
            m_data = m_alloc->alloc<T>(m_size);
            new (m_data) T[m_size];
        }

        DynArray(size_type n, T def) noexcept(std::is_nothrow_copy_constructible_v<T>)
            : m_size(n)
            , m_capacity(n)
        {
            m_data = m_alloc->alloc<T>(m_size);
            std::fill_n(m_data, m_size, def);
        }

        constexpr auto size() const noexcept -> size_type { return m_size; }
        constexpr auto empty() const noexcept -> bool { return m_size == 0; }
        constexpr auto capacity() const noexcept -> size_type { return m_capacity; }
        constexpr auto data() const noexcept -> const_pointer { return m_data; }
        constexpr auto data() noexcept -> pointer { return m_data; }
        constexpr auto alloc() const noexcept -> BlockAllocator const* { return m_alloc; }
        constexpr auto alloc() noexcept -> BlockAllocator* { return m_alloc; }

        template <typename... Args>
        auto emplace_back(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
            grow_if_need(1); 
            auto* ptr = std::launder(reinterpret_cast<std::byte*>(m_data + m_size++));
            new (ptr) T(std::forward<T>(args)...);
        }

        auto push_back(T val) noexcept(std::is_nothrow_copy_constructible_v<T>) {
            grow_if_need(1);
            auto* ptr = std::launder(reinterpret_cast<std::byte*>(m_data + m_size++));
            new (ptr) T(std::move(val));
        }

        constexpr auto back() const noexcept -> const_reference { return m_data[size() - 1]; }
        constexpr auto back() noexcept -> reference { return m_data[size() - 1]; }
        constexpr auto front() const noexcept -> const_reference { return m_data[0]; }
        constexpr auto front() noexcept -> reference { return m_data[0]; }
        constexpr auto begin() noexcept -> iterator {
            return { m_data };
        }

        constexpr auto end() noexcept -> iterator {
            return { m_data + m_size };
        }

        constexpr auto begin() const noexcept -> const_iterator {
            return { m_data };
        }

        constexpr auto end() const noexcept -> const_iterator {
            return { m_data + m_size };
        }

        constexpr auto rbegin() noexcept -> reverse_iterator {
            return std::reverse_iterator(begin());
        }

        constexpr auto rend() noexcept -> reverse_iterator {
            return std::reverse_iterator(end());
        }

        constexpr auto rbegin() const noexcept -> const_reverse_iterator {
            return std::reverse_iterator(begin());
        }

        constexpr auto rend() const noexcept -> const_reverse_iterator {
            return std::reverse_iterator(end());
        }

        constexpr auto operator[](size_type k) const noexcept -> const_reference {
            assert(k < size());
            return m_data[k];
        }

        constexpr auto operator[](size_type k) noexcept -> reference {
            assert(k < size());
            return m_data[k];
        }

        constexpr auto find(T const& val) const noexcept -> iterator requires std::equality_comparable<T> {
            for (auto i = 0ul; i < size(); ++i) {
                if (m_data[i] == val) return begin() + i;
            }
            return end();
        }

        constexpr auto binary_search(T const& val) const noexcept -> iterator requires (std::equality_comparable<T> && std::totally_ordered<T>) {
            auto l = size_type{};
            auto r = size();
            while (l < r) {
                auto mid = std::midpoint(l, r);
                auto const& el = m_data[mid];
                if (el == val) return begin() + mid;
                else if (el < val) l = mid + 1;
                else r = mid;
            }
            return end();
        }

        auto erase(size_type pos) noexcept(std::is_nothrow_move_assignable_v<T>) -> T {
            assert(pos < size());
            if (pos + 1 == size()) return pop_back();
            auto val = std::move(m_data[pos]);
            std::move(begin() + pos + 1, end(), begin() + pos);
            --m_size;
            return std::move(val);
        }

        auto erase(size_type pos, size_type size) noexcept(std::is_nothrow_move_assignable_v<T>) -> void {
            pos = std::min(pos, m_size);
            size = std::clamp(size, pos, m_size);
            if (pos == size) return;

            if (size == m_size) {
                for (auto i = pos; i < size; ++i) m_data[i].~T();
                m_size = pos;
                return;
            }
            // |xxxxx|......|xxx|
            //       ^      ^
            //       |      |
            //      pos    size

            auto sz = std::min(m_size - pos, size);
            auto start = pos + sz;
            auto end = std::min(m_size, start + size);
            std::move(begin() + start, begin() + end, begin() + pos);
            for (auto i = pos + sz; i < m_size; ++i) {
                m_data[i].~T();
            }
            m_size -= sz;
        }

        auto erase(iterator b, iterator e) noexcept(std::is_nothrow_move_assignable_v<T>) -> void {
            auto start = std::distance(this->begin(), b);
            auto size = std::distance(b, e);
            erase(static_cast<size_type>(start), static_cast<size_type>(size));
        }

        auto pop_back() noexcept(std::is_nothrow_move_assignable_v<T>) -> T {
            assert(!empty());
            auto val = std::move(back());
            --m_size;
            return std::move(val);
        }

        auto resize(size_type n, T def = {}) noexcept(std::is_nothrow_copy_constructible_v<T>) {
            if (capacity() < n) {
                m_data = m_alloc->realloc<T>(m_data, m_size, n);
                m_capacity = std::max(m_size, n);
            }

            for (auto i = m_size; i < n; ++i) m_data[i] = def;
            m_size = n;
        }

        auto reserve(size_type n) noexcept {
            if (capacity() < n) {
                m_data = m_alloc->realloc<T>(m_data, capacity(), n);
                m_capacity = std::max(capacity(), n);
            }
        }

        auto insert(size_type pos, T val) noexcept {
            assert(pos < size());
            grow_if_need(1);
            if (pos + 1 == size()) {
                m_data[m_size++] = std::move(val);
                return;
            }
            std::move(begin() + pos, end(), begin() + pos + 1);
            ++m_size;
            m_data[pos] = std::move(val);
        }

        template <typename OtherIterator>
        auto insert(iterator pos, OtherIterator b, OtherIterator e) noexcept -> void {
            if (b == e) return;
            auto size = static_cast<size_type>(std::distance(b, e));
            grow_if_need(size);
            std::move(pos, end(), pos + size);
            std::move(b, e, pos);
            m_size += size;
        }

        constexpr auto operator==(DynArray const& other) const noexcept -> bool {
            if (size() != other.size()) return false;
            return std::equal(begin(), end(), other.begin());
        }

        constexpr auto operator!=(DynArray const& other) const noexcept -> bool {
            return !(*this == other);
        }

        constexpr operator std::span<T>() const noexcept {
            return { const_cast<T*>(data()), size() };
        }

        friend constexpr auto swap(DynArray& lhs, DynArray& rhs) noexcept -> void {
            using std::swap;
            swap(lhs.m_data, rhs.m_data);
            swap(lhs.m_size, rhs.m_size);
            swap(lhs.m_alloc, rhs.m_alloc);
        }
    private:
        auto grow_if_need(size_type extra) noexcept {
            auto cap = capacity();
            if (cap >= size() + extra) return;
            while (cap < m_size + extra) cap *= 2;
            m_data = m_alloc->realloc(m_data, capacity(), cap);
            m_capacity = cap;
        }
    private:
        pointer         m_data{};
        size_type       m_size{};
        size_type       m_capacity{};
        BlockAllocator* m_alloc{AllocatorManager::instance().get_alloc()};
    };

} // namespace tpl

#endif // AMT_TPL_DYNAMIC_ARRAY_HPP
