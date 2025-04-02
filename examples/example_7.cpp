#include <chrono>
#include <cstdlib>
#include <ctime>
#include <print>

#include "tpl.hpp"

using namespace tpl;

int main() {
    Scheduler s;
    auto i = 0ul;
    auto ts = s
        | [&i] (TaskToken& t) {
            ++i;
            // One shot independent work that could run on may thread
            t.queue_work([&i] noexcept {
                std::println("Non-Awaitable work[{}]: {}", i, ThisThread::pool_id());
            });
            t.awaitable_queue_work([&i] noexcept {
                ThisThread::sleep_for(std::chrono::seconds(1));
                std::println("Awaitable work[{}]: {}", i, ThisThread::pool_id());
            }).await();
            t.schedule();
        }
        ;
    ts.run();
}
