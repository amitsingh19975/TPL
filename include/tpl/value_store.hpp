#ifndef AMT_TPL_VALUE_STORE_HPP
#define AMT_TPL_VALUE_STORE_HPP

#include "allocator.hpp"
#include <cstring>
#include <functional>
#include <type_traits>
#include <expected>
#include <utility>
#include <atomic>

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
            , m_values(Cap)
        {}
        ValueStore(ValueStore const&) noexcept = delete;
        ValueStore(ValueStore &&) noexcept = delete;
        ValueStore& operator=(ValueStore const&) noexcept = delete;
        ValueStore& operator=(ValueStore &&) noexcept = delete;
        ~ValueStore() noexcept = default;

        template <typename T>
            requires (std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>)
        auto put(task_id id, T&& value) -> void {
            if (id >= m_values.size()) return;
            auto tmp = m_allocator->alloc<T>();
            if constexpr (std::is_move_constructible_v<T>) {
                new(tmp) T(std::move(value));
            } else {
                new(tmp) T(value);
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
            if (id >= m_values.size()) return std::unexpected(ValueStoreError::not_found);
            Value tmp = m_values[id];

            if (!tmp.destroy) {
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
            if (id >= m_values.size()) return std::unexpected(ValueStoreError::not_found);
            Value tmp = m_values[id];

            if (!tmp.destroy) {
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
            if (id >= m_values.size()) return std::unexpected(ValueStoreError::not_found);
            Value tmp = std::exchange(m_values[id], Value{});

            if (!tmp.destroy) {
                return std::unexpected(ValueStoreError::not_found);
            }

            if (internal::ValueStoreDestructor<T>::destroy != tmp.destroy) {
                return std::unexpected(ValueStoreError::type_mismatch);
            }
            m_size.fetch_sub(1);
            auto ptr = reinterpret_cast<T*>(tmp.value);
            auto val = std::move(*ptr);
            m_allocator->dealloc(ptr);
            return std::move(val);
        }

        auto remove(task_id id) noexcept -> void {
            if (id >= m_values.size()) return;
            Value v = std::exchange(m_values[id], Value{});
            if (!v.value) return;
            v.destroy(v.value);
            m_size.fetch_sub(1);
        }

        auto clear() noexcept -> void {
            for (auto& v: m_values) {
                if (!v.value) continue;
                v.destroy(v.value);
                v.destroy = nullptr;
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

        constexpr auto get_type(task_id id) const noexcept {
            assert(id < m_values.size());
            return m_values[id].destroy;
        }

        auto resize(std::size_t sz) {
            m_values.resize(sz);
        }
    private:
        struct Value {
            void* value{nullptr};
            void (*destroy)(void*);
        };
    private:
        BlockAllocator* m_allocator{nullptr};
        std::vector<Value> m_values;
        std::atomic<std::size_t> m_size{0};
    };

} // namespace tpl

#endif // AMT_TPL_VALUE_STORE_HPP
