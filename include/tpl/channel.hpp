#ifndef AMT_TPL_CHANNEL_HPP
#define AMT_TPL_CHANNEL_HPP

#include <atomic>
#include <expected>
#include "queue.hpp"
#include "tpl/allocator.hpp"
#include "waiter.hpp"

namespace tpl {

    enum class ChannelError {
        closed
    };

    constexpr auto to_string(ChannelError e) noexcept -> std::string_view {
        switch (e) {
            case ChannelError::closed: return "Channel is closed";
        }
    }

    template <typename Container>
    struct BasicChannel {
        using base_type = Container;
        using size_type = std::size_t;
        using value_type = typename base_type::value_type;

        BasicChannel() noexcept = default;
        BasicChannel(BasicChannel const&) noexcept = delete;
        BasicChannel(BasicChannel &&) noexcept = delete;
        BasicChannel& operator=(BasicChannel const&) noexcept = delete;
        BasicChannel& operator=(BasicChannel &&) noexcept = delete;
        ~BasicChannel() = default;

        constexpr auto size() const noexcept -> size_type {
            return m_queue.size();
        }

        constexpr auto empty() const noexcept -> bool {
            return m_queue.empty();
        }

        auto try_send(value_type const& val) -> std::expected<bool, ChannelError> {
            if (is_closed()) return std::unexpected(ChannelError::closed);
            if constexpr (!internal::is_bounded_queue_v<base_type>) {
                m_queue.push(val);
                m_waiter.notify_all();
                return true;
            } else {
                if (m_queue.push(val)) {
                    m_waiter.notify_all();
                    return true;
                }
                return false;
            }
        }

        auto try_send(value_type && val) -> std::expected<bool, ChannelError> {
            if (is_closed()) return std::unexpected(ChannelError::closed);
            if constexpr (!internal::is_bounded_queue_v<base_type>) {
                m_queue.push(std::move(val));
                m_waiter.notify_all();
                return true;
            } else {
                if (m_queue.push(std::move(val))) {
                    m_waiter.notify_all();
                    return true;
                }
                return false;
            }
        }

        auto send(value_type const& val) -> std::expected<void, ChannelError> {
            if (is_closed()) return std::unexpected(ChannelError::closed);
            if constexpr (!internal::is_bounded_queue_v<base_type>) {
                m_queue.push(val);
                m_waiter.notify_all();
            } else {
                while (true) {
                    if (m_queue.push(val)) {
                        m_waiter.notify_all();
                        return {};
                    }
                    m_waiter.wait([this] {
                        return !m_queue.full() || m_closed.load(std::memory_order_acquire);
                    });
                }
            }
            return {};
        }

        auto send(value_type &&val)  -> std::expected<void, ChannelError> {
            if (is_closed()) return std::unexpected(ChannelError::closed);
            if constexpr (!internal::is_bounded_queue_v<base_type>) {
                m_queue.push(std::move(val));
                m_waiter.notify_all();
            } else {
                while (true) {
                    if (m_queue.push(val)) {
                        m_waiter.notify_all();
                        return {};
                    }

                    m_waiter.wait([this] {
                        return !m_queue.full() || m_closed.load(std::memory_order_acquire);
                    });
                }
            }
            return {};
        }

        auto try_receive() -> std::optional<value_type> {
            return m_queue.pop();
        }

        auto receive() -> std::optional<value_type> {
            while (true) {
                auto val = m_queue.pop();
                if (val) {
                    m_waiter.notify_all();
                    return { std::move(*val) };
                }

                m_waiter.wait([this] {
                    return !m_queue.empty() || m_closed.load(std::memory_order_acquire);
                });
            }
            return {};
        }

        constexpr auto close() noexcept -> void {
            m_closed.store(true, std::memory_order_release);
            m_waiter.notify_all();
        }

        constexpr auto is_closed() const noexcept -> bool {
            return m_closed.load(std::memory_order_relaxed);
        }

    private:
        base_type m_queue;
        std::atomic<bool> m_closed{false};
        internal::Waiter m_waiter;
    };

    template <typename T, std::size_t N>
    using bounded_channel_t = BasicChannel<BoundedQueue<T, N>>;

    template <typename T, std::size_t BlockSize = 256>
    using channel_t = BasicChannel<Queue<T, BlockSize>>;

} // namespace tpl

#endif // AMT_TPL_CHANNEL_HPP
