#ifndef AMT_TPL_COW_HPP
#define AMT_TPL_COW_HPP

#include <cstdint>
#include <functional>
#include <limits>
#include <cassert>
#include <type_traits>

namespace tpl {

    template <typename T>
    struct Cow {
        using value_type = std::remove_cvref_t<T>;
        using reference = value_type&;
        using const_reference = value_type const&;
        using pointer = value_type*;
        using const_pointer = value_type const*;
        using size_type = std::size_t;
    private:
        static constexpr auto npos = std::numeric_limits<size_type>::max();
        enum ValueIndex {
            Owned = 0,
            Borrowed = 1,
            None = npos
        };
        static constexpr auto size = std::max(sizeof(value_type), sizeof(std::uintptr_t));
    public:
        Cow(T const& val) noexcept(std::is_nothrow_copy_constructible_v<T>) requires std::is_copy_constructible_v<T>
            : m_index(Owned)
        {
            new(m_data) T(val);
        }
        Cow(T&& val) noexcept(std::is_nothrow_move_constructible_v<T>) requires std::is_move_constructible_v<T>
            : m_index(Owned)
        {
            new(m_data) T(std::move(val));
        }
        Cow(pointer val) noexcept
            : m_index(Borrowed)
        {
            *reinterpret_cast<std::uintptr_t*>(m_data) = reinterpret_cast<std::uintptr_t>(val);
        }
        Cow(reference val) noexcept
            : m_index(Borrowed)
        {
            *reinterpret_cast<std::uintptr_t*>(m_data) = reinterpret_cast<std::uintptr_t>(&val);
        }
        Cow(std::reference_wrapper<T> val) noexcept
            : Cow(val.get())
        {}

        constexpr Cow(Cow const& other) requires (std::is_trivially_copy_constructible_v<T>)
            : m_index(other.m_index)
        {
            std::memcpy(m_data, other.m_data, size);
        }
        constexpr Cow(Cow const&) = delete;
        constexpr Cow(Cow && other) noexcept
            : m_index(other.m_index)
        {
            std::memcpy(m_data, other.m_data, size);
            other.m_index = None;
        }
        constexpr Cow& operator=(Cow const& other) requires (std::is_trivially_copy_constructible_v<T>)
        {
            if (this == &other) return *this;
            std::memcpy(m_data, other.m_data, size);
            m_index = other.m_index;
            return *this;
        }
        constexpr Cow& operator=(Cow const&) = delete;
        constexpr Cow& operator=(Cow &&other) noexcept {
            if (this == &other) return *this;
            std::memcpy(m_data, other.m_data, size);
            m_index = other.m_index;
            other.m_index = None;
            return *this;
        }
        constexpr ~Cow() noexcept(std::is_nothrow_destructible_v<T>) requires std::is_trivially_destructible_v<T> = default;
        ~Cow() noexcept(std::is_nothrow_destructible_v<T>) {
            if (m_index == Owned) {
                pointer val = reinterpret_cast<pointer>(m_data);
                val->~T();
            }
        }

        constexpr auto get() noexcept -> pointer {
            assert((m_index != None) && "Value does not exist.");

            if (m_index == Owned) {
                return reinterpret_cast<pointer>(m_data);
            } else if (m_index == Borrowed) {
                auto tmp = *reinterpret_cast<std::uintptr_t*>(m_data);
                return reinterpret_cast<pointer>(tmp);
            } else {
                return nullptr;
            }
        }

        constexpr auto get() const noexcept -> const_pointer {
            assert((m_index != None) && "Value does not exist.");

            if (m_index == Owned) {
                return reinterpret_cast<const_pointer>(m_data);
            } else if (m_index == Borrowed) {
                return reinterpret_cast<const_pointer>(
                    *reinterpret_cast<std::uintptr_t const*>(m_data)
                );
            } else {
                return nullptr;
            }
        }

        constexpr auto ref() noexcept -> reference {
            return *get();
        }

        constexpr auto ref() const noexcept -> const_reference {
            return *get();
        }

        constexpr auto take() noexcept -> value_type {
            auto val = std::move(*get());
            m_index = None;
            return std::move(val);
        }

        constexpr auto is_owned() const noexcept -> bool { return m_index == Owned; }
        constexpr auto is_borrowed() const noexcept -> bool { return m_index == Borrowed; }

        constexpr operator bool() const noexcept {
            return m_index != None;
        }

    private:
        std::byte m_data[size];
        std::size_t m_index{None};
    };

    template <>
    struct Cow<std::string> {
        using value_type = std::string;
        using reference = std::string_view;
        using const_reference = std::string_view const;
        using pointer = char*;
        using const_pointer = char const*;
        using size_type = std::size_t;
    private:
        static constexpr auto npos = std::numeric_limits<size_type>::max();
        enum ValueIndex {
            Owned = 0,
            Borrowed = 1,
            None = npos
        };
        static constexpr auto size = std::max(sizeof(value_type), sizeof(std::string_view));
    public:
        Cow(value_type const& val)
            : m_index(Owned)
        {
            new(m_data) value_type(val);
        }
        Cow(value_type&& val)
            : m_index(Owned)
        {
            new(m_data) value_type(std::move(val));
        }
        Cow(std::string_view val) noexcept
            : m_index(Borrowed)
        {
            new(m_data) std::string_view(std::move(val));
        }

        Cow(std::string* val)
            : m_index(Borrowed)
        {
            new(m_data) reference(*val);
        }

        Cow(std::string const* val)
            : m_index(Owned)
        {
            new(m_data) reference(*val);
        }

        ~Cow() {
            if (m_index == Owned) {
                value_type* val = reinterpret_cast<value_type*>(m_data);
                val->~value_type();
            }
        }

        constexpr auto get() noexcept -> std::string_view {
            assert((m_index != None) && "Value does not exist.");

            if (m_index == Owned) {
                return *reinterpret_cast<std::string*>(m_data);
            } else if (m_index == Borrowed) {
                return *reinterpret_cast<std::string_view*>(m_data);
            } else {
                return {};
            }
        }

        constexpr auto get() const noexcept -> std::string_view {
            assert((m_index != None) && "Value does not exist.");

            if (m_index == Owned) {
                return *reinterpret_cast<std::string const*>(m_data);
            } else if (m_index == Borrowed) {
                return *reinterpret_cast<std::string_view const*>(m_data);
            } else {
                return {};
            }
        }

        constexpr auto ref() noexcept -> std::string_view {
            return get();
        }

        constexpr auto ref() const noexcept -> std::string_view {
            return get();
        }

        constexpr auto take() noexcept -> value_type {
            if (m_index == Owned) {
                auto tmp = reinterpret_cast<std::string*>(m_data);
                m_index = None;
                return std::move(*tmp);
            } else if (m_index == Borrowed) {
                auto tmp = *reinterpret_cast<std::string_view*>(m_data);
                m_index = None;
                return std::string(tmp);
            } else {
                return {};
            }
        }

        constexpr auto is_owned() const noexcept -> bool { return m_index == Owned; }
        constexpr auto is_borrowed() const noexcept -> bool { return m_index == Borrowed; }

        constexpr operator bool() const noexcept {
            return m_index != None;
        }

    private:
        std::byte m_data[size];
        std::size_t m_index{None};
    };
} // namespace tpl

#endif // AMT_TPL_COW_HPP
