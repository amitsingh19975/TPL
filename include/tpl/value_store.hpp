#ifndef AMT_TPL_VALUE_STORE_HPP
#define AMT_TPL_VALUE_STORE_HPP

#include "allocator.hpp"
#include <mutex>
#include <unordered_map>
#include <expected>

namespace tpl {

    namespace internal {
        template <typename T>
        struct meta_inner {
            using type = T;

            static void id() {};
        };

        template <typename T>
        static constexpr auto meta = meta_inner<T>::id;
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

    template <typename TaskId>
    struct ValueStore {
        constexpr ValueStore(BlockAllocator* allocator) noexcept
            : m_allocator(allocator)
        {}
        constexpr ValueStore(ValueStore const&) noexcept = delete;
        constexpr ValueStore(ValueStore &&) noexcept = default;
        constexpr ValueStore& operator=(ValueStore const&) noexcept = delete;
        constexpr ValueStore& operator=(ValueStore &&) noexcept = default;
        constexpr ~ValueStore() noexcept = default;

        template <typename T>
        auto put(TaskId id, T&& value) {
            auto tmp = m_allocator->alloc<T>();
            new(tmp) T(std::move(value));
            std::lock_guard scope(m_mutex);
            if (auto it = m_values.find(id); it == m_values.end()) {
                m_values[id] = Value {
                    .type_id = internal::meta<T>,
                    .value = tmp,
                    .destructor = +[](void* val) -> void {
                        reinterpret_cast<T*>(val)->~T();
                    }
                };
            } else {
                auto ptr = it->second.value;
                it->second.destructor(ptr);
                m_allocator->dealloc(ptr);
                it->second = Value {
                    .type_id = internal::meta<T>,
                    .value = tmp,
                    .destructor = +[](void* val) -> void {
                        reinterpret_cast<T*>(val)->~T();
                    }
                };
            }
        }

        template <typename T>
        auto get(TaskId id) noexcept -> std::expected<T, ValueStoreError> {
            std::lock_guard scope(m_mutex);
            if (auto it = m_values.find(id); it != m_values.end()) {
                Value tmp = it->second;
                if (internal::meta<T> != tmp.type_id) {
                    return std::unexpected(ValueStoreError::type_mismatch);
                }
                m_values.erase(it);
                auto ptr = reinterpret_cast<T*>(tmp.value);
                auto val = std::move(*ptr);
                m_allocator->dealloc(ptr);
                return std::move(val);
            } else {
                return std::unexpected(ValueStoreError::not_found);
            }
        }

        auto remove(TaskId id) noexcept {
            std::lock_guard scope(m_mutex);
            if (auto it = m_values.find(id); it != m_values.end()) {
                Value tmp = it->second;
                m_values.erase(it);
                tmp.destructor(tmp.value);
                m_allocator->dealloc(tmp.value);
            }
        }

        auto clear() noexcept {
            std::lock_guard scope(m_mutex);
            m_values.clear();
            m_allocator->reset(true);
        }

    private:
        struct Value {
            void (*type_id)() {internal::meta<void>};
            void* value{nullptr};
            void (*destructor)(void*);
        };
    private:
        BlockAllocator* m_allocator{nullptr};
        std::unordered_map<TaskId, Value> m_values;
        std::mutex m_mutex;
    };

} // namespace tpl

#endif // AMT_TPL_VALUE_STORE_HPP
