#ifndef AMT_TPL_VALUE_STORE_HPP
#define AMT_TPL_VALUE_STORE_HPP

#include "allocator.hpp"
#include "tpl/basic.hpp"
#include "tpl/dyn_array.hpp"
#include <cstdint>
#include <cstring>
#include <functional>
#include <type_traits>
#include <expected>
#include <utility>
#include <atomic>
#include <vector>

namespace tpl {

    namespace internal {
        template <typename T>
        struct ValueStoreDestructor {
            static void destroy(void* ptr) noexcept {
                if constexpr (
                    !std::is_trivially_destructible_v<T>
                ) {
                    auto& tmp = *reinterpret_cast<T*>(ptr);
                    tmp.~T();
                }
            }
        };
    } // namespace internal

    enum class ValueStoreError {
        type_mismatch,
        not_found
    };

    constexpr auto to_string(ValueStoreError e) noexcept -> std::string_view {
        switch (e) {
            case ValueStoreError::type_mismatch: return "Type Mismatch";
            case ValueStoreError::not_found: return "Not Found";
        }
    }

    // NOTE: This is not a thread-safe.
    struct ValueStore {
        using task_id = std::size_t;

        ValueStore(std::size_t Cap, BlockAllocator* allocator) noexcept
            : m_allocator(allocator)
            , m_values(Cap, Value{}, allocator)
        {}
        ValueStore(ValueStore const&) noexcept = delete;
        ValueStore(ValueStore &&) noexcept = delete;
        ValueStore& operator=(ValueStore const&) noexcept = delete;
        ValueStore& operator=(ValueStore &&) noexcept = delete;
        ~ValueStore() noexcept = default;

        template <typename T>
            requires (std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>)
        auto put(task_id id, T&& value) {
            void* tmp{nullptr};
            if constexpr (sizeof(T) < sizeof(std::uintptr_t) && std::is_trivially_destructible_v<T>) {
                tmp = reinterpret_cast<void*>(value);
            } else {
                tmp = m_allocator->alloc<T>();
                if constexpr (std::is_move_constructible_v<T>) {
                    new(tmp) T(std::move(value));
                } else {
                    new(tmp) T(value);
                }
            }

            remove(id);
            std::exchange(m_values[id], Value {
                .value = tmp,
                .destroy = internal::ValueStoreDestructor<T>::destroy
            });
            m_size.fetch_add(1);
        }

        // std::expected does not allow references so we wrap it in reference wrapper
        template <typename T>
        auto get(task_id id) noexcept -> std::expected<std::reference_wrapper<T>, ValueStoreError> {
            Value tmp = m_values[id];

            if (!tmp.value) {
                return std::unexpected(ValueStoreError::not_found);
            }

            if (internal::ValueStoreDestructor<T>::destroy != tmp.destroy) {
                return std::unexpected(ValueStoreError::type_mismatch);
            }
            auto ptr = reinterpret_cast<T*>(tmp.value);
            return std::ref(*ptr);
        }

        template <typename T>
        auto get(task_id id) const noexcept -> std::expected<std::reference_wrapper<T const&>, ValueStoreError> {
            Value tmp = m_values[id];

            if (!tmp.value) {
                return std::unexpected(ValueStoreError::not_found);
            }

            if (internal::ValueStoreDestructor<T>::destroy != tmp.destroy) {
                return std::unexpected(ValueStoreError::type_mismatch);
            }
            auto ptr = reinterpret_cast<T*>(tmp.value);
            return std::ref(*ptr);
        }

        template <typename T>
            requires (std::is_move_constructible_v<T>)
        auto consume(task_id id) noexcept -> std::expected<T, ValueStoreError> {
            Value tmp = std::exchange(m_values[id], Value{});

            if (!tmp.value) {
                return std::unexpected(ValueStoreError::not_found);
            }

            if (internal::ValueStoreDestructor<T>::destroy != tmp.destroy) {
                return std::unexpected(ValueStoreError::type_mismatch);
            }
            m_size.fetch_sub(1);
            if constexpr (sizeof(T) < sizeof(std::uintptr_t) && std::is_trivially_destructible_v<T>) {
                auto t0 = reinterpret_cast<std::uintptr_t>(tmp.value);
                auto t1 = static_cast<internal::storage_value<sizeof(T)>::type>(t0);
                return std::bit_cast<T>(t1);
            } else {
                auto ptr = reinterpret_cast<T*>(tmp.value);
                auto val = std::move(*ptr);
                m_allocator->dealloc(ptr);
                return std::move(val);
            }
        }

        auto remove(task_id id) noexcept -> void {
            Value v = std::exchange(m_values[id], Value{});
            if (!v.value) return;
            v.destroy(v.value);
            m_size.fetch_sub(1);
        }

        auto clear() noexcept -> void {
            for (auto& v: m_values) {
                if (!v.value) continue;
                v.destroy(std::exchange(v.value, nullptr));
            }
            m_allocator->reset(true);
            m_size = 0;
        }

        auto empty() const noexcept -> bool {
            return m_size.load() == 0;
        }

        auto size() const noexcept -> std::size_t {
            return m_size.load();
        }

    private:
        struct Value {
            void* value{nullptr};
            void (*destroy)(void*);
        };
    private:
        BlockAllocator* m_allocator{nullptr};
        DynArray<Value> m_values;
        std::atomic<std::size_t> m_size{0};
    };

} // namespace tpl

#endif // AMT_TPL_VALUE_STORE_HPP
