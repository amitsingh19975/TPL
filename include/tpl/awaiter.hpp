#ifndef TPL_AMT_AWAITER_HPP
#define TPL_AMT_AWAITER_HPP

#include "waiter.hpp"
#include <cassert>
#include <memory>
#include <optional>
#include <type_traits>

namespace tpl {

    template <typename T>
    struct Awaiter {
        using value_type = std::conditional_t<std::is_void_v<T>, void*, T>;
        auto await() -> value_type requires (!std::is_void_v<T>) {
            wait();
            return get_value();
        }

        auto await() -> void requires (std::is_void_v<T>) {
            wait();
        }

    private:
        auto wait() -> void {
            if (!m_data->finished) {
                m_data->waiter.wait([this] {
                    return m_data->finished;
                });
            }
        }

        auto get_value() -> value_type {
            auto val = *m_data->value;
            m_data->value = std::nullopt;
            return val;
        }

        friend struct Scheduler;

        struct Wrapper {
            internal::Waiter waiter{};
            std::optional<value_type> value;
            bool finished{false};

            auto notify_value(value_type val)
                noexcept (std::is_nothrow_move_constructible_v<value_type>)
                requires (!std::is_void_v<T>)
            {
                value = std::move(val);
                waiter.notify_all([this]{
                    finished = true;
                });
            }

            auto notify_value()
                noexcept (std::is_nothrow_move_constructible_v<value_type>)
                requires (std::is_void_v<T>)
            {
                waiter.notify_all([this]{
                    finished = true;
                });
            }
        };
    private:
        std::unique_ptr<Wrapper> m_data{ std::make_unique<Wrapper>() };
    };

} // namespace tpl

#endif // TPL_AMT_AWAITER_HPP
