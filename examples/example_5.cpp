#include <cstdlib>
#include <print>

#include "tpl.hpp"

using namespace tpl;

int main() {
    Scheduler s;
    auto ts = s
        | []{ std::println("Task 0: {}", ThisThread::pool_id()); }
        | []{ std::println("Task 1: {}", ThisThread::pool_id()); }
        | []{ std::println("Task 2: {}", ThisThread::pool_id()); };

    if (!ts) {
        std::println("Error: {}", to_string(ts.error()));
        return 1;
    }

    [[maybe_unused]] auto res = s.run();
}
