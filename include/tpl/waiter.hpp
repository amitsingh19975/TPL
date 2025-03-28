#ifndef AMT_TPL_WAITER_HPP
#define AMT_TPL_WAITER_HPP

#include <chrono>
#include <condition_variable>
#include <mutex>
namespace tpl::internal {

    struct Waiter {
        mutable std::mutex mutex;
        mutable std::condition_variable cv;

        void notify_all() const {
            std::lock_guard lock(mutex);
            cv.notify_all();
        }

        void notify_all(auto&& fn) const {
            std::lock_guard lock(mutex);
            fn();
            cv.notify_all();
        }

        void notify_one() const {
            std::lock_guard lock(mutex);
            cv.notify_one();
        }

        void notify_one(auto&& fn) const {
            std::lock_guard lock(mutex);
            fn();
            cv.notify_one();
        }

        template <typename Fn>
        auto wait(Fn&& cond) const -> bool {
            std::unique_lock lock(mutex);
            cv.wait(lock, std::forward<Fn>(cond));
            return true;
        }

        template <typename Fn>
        auto wait_for(std::chrono::nanoseconds ns, Fn&& cond) const -> bool {
            std::unique_lock lock(mutex);
            cv.wait_for(lock, ns, std::forward<Fn>(cond));
            return true;
        }
    };

} // namespace tpl::internal

#endif // AMT_TPL_WAITER_HPP
