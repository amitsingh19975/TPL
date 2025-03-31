#include <cstdlib>
#include <print>

#include "tpl.hpp"

using namespace tpl;

int main() {
    Scheduler s;
    auto ts = s
        | []{ std::println("Task 0: {}", ThisThread::pool_id()); }
        | []{ std::println("Task 1: {}", ThisThread::pool_id()); }
        | TaskGroup {
           []{ std::println("Task 2: {}", ThisThread::pool_id()); },
           []{ std::println("Task 3: {}", ThisThread::pool_id()); },
           []{ std::println("Task 4: {}", ThisThread::pool_id()); }
        } > []{ std::println("Task 1.5: {}", ThisThread::pool_id()); }
        ;

    ts.run();
}
