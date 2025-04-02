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
            // One shot independent work that could run on may thread
            t.queue_work([&i] noexcept {
                std::println("Run[{}]: {}", ++i, ThisThread::pool_id());
            });
            ThisThread::sleep_for(std::chrono::seconds(1));
            t.schedule();
        }
        ;
    ts.run();
}
