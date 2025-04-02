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
            t.queue_work([&i] noexcept {
                std::println("Run[{}]: {}", ++i, ThisThread::pool_id());
            });
            ThisThread::sleep_for(std::chrono::seconds(1));
            t.schedule();
        }
        ;
    ts.run();
}
