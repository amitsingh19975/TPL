#ifndef AMT_TPL_VALUE_STORE_HPP
#define AMT_TPL_VALUE_STORE_HPP

#include "allocator.hpp"
#include "task_id.hpp"
#include "cow.hpp"
#include "list.hpp"
#include <cstring>
#include <type_traits>
#include <expected>
#include <utility>
#include <atomic>

namespace tpl {

    namespace internal {
        template <typename T>
        struct ValueStoreDestructor {
            static void destroy(void* ptr) noexcept {
                if (!ptr) return;
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
        ValueStore(BlockAllocator* allocator) noexcept
            : m_allocator(allocator)
        {}
        ValueStore(ValueStore const&) noexcept = delete;
        ValueStore(ValueStore &&) noexcept = delete;
        ValueStore& operator=(ValueStore const&) noexcept = delete;
        ValueStore& operator=(ValueStore &&) noexcept = delete;
        ~ValueStore() noexcept = default;

        template <typename T>
            requires (std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>)
        auto put(TaskId task_id, T&& value) -> void {
            auto id = tid_to_int(task_id);

            if (id >= m_values.size()) return;
            auto tmp = m_allocator->alloc<T>();
            if constexpr (std::is_move_constructible_v<T>) {
                new(tmp) T(std::move(value));
            } else {
                new(tmp) T(value);
            }

            remove(task_id);
            m_values[id] = Value {
                .value = tmp,
                .destroy = internal::ValueStoreDestructor<T>::destroy
            };
            m_size.fetch_add(1);
        }

        // std::expected does not allow references so we wrap it in reference wrapper
        template <typename T>
        auto get(TaskId task_id) noexcept -> std::expected<Cow<T>, ValueStoreError> {
            auto id = tid_to_int(task_id);

            if (id >= m_values.size()) return std::unexpected(ValueStoreError::not_found);
            Value tmp = m_values[id];

            if (!tmp.destroy) {
                return std::unexpected(ValueStoreError::not_found);
            }

            if (internal::ValueStoreDestructor<T>::destroy != tmp.destroy) {
                return std::unexpected(ValueStoreError::type_mismatch);
            }
            auto ptr = reinterpret_cast<T*>(tmp.value);
            return Cow<T>(ptr);
        }

        template <typename T>
            requires (std::is_move_constructible_v<T>)
        auto consume(TaskId task_id) noexcept -> std::expected<Cow<T>, ValueStoreError> {
            auto id = tid_to_int(task_id);
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
            return Cow<T>(std::move(val));
        }

        auto remove(TaskId task_id) noexcept -> void {
            auto id = tid_to_int(task_id);
            if (id >= m_values.size()) return;
            Value v = std::exchange(m_values[id], Value{});
            if (!v.destroy) return;
            v.destroy(v.value);
            m_size.fetch_sub(1);
        }

        auto clear() noexcept -> void {
            auto sz = m_values.size();
            for (auto i = 0ul; i < sz; ++i) {
                auto& v = m_values[i];
                if (!v.destroy) continue;
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

        constexpr auto get_type(TaskId task_id) const noexcept {
            auto id = tid_to_int(task_id);
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
        BlockSizedList<Value> m_values;
        std::atomic<std::size_t> m_size{0};
    };

} // namespace tpl

#endif // AMT_TPL_VALUE_STORE_HPP
