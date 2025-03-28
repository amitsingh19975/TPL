#include <cstdlib>
#include <print>

#include "tpl.hpp"

using namespace tpl;

template <typename T, typename E>
void print(std::expected<T, E> const& v) {
    if (v) std::println("Value: {}", *v);
    else std::println("Error: {}", to_string(v.error()));
}

int main() {
    Scheduler s;
    auto t0 = s.add_task([](TaskToken& token) {
        auto value = token.arg<int>(TaskId(1)).value_or(-1);
        std::println("Hello from task 0: Called after => {}", value);
        token.stop();
        return 0;
    });
    auto i = 0ul;
    auto t1 = s.add_task([&i](TaskToken& token) {
        std::println("[{}]: Hello from task 1", i);
        if (i ++ < 5) {
            token.schedule();
        }
        return 1;
    });
    auto t2 = s.add_task([](TaskToken& token) {
        auto value = token.arg<int>(TaskId(0)).value_or(-1);
        std::println("Hello from task 2: Called after => {}", value);
        return 2;
    });
    {
        auto e0 = t0.deps_on(t1);
        if (!e0) std::println("Dep Error: {}", to_string(e0.error()));
    }
    {
        auto e0 = t2.deps_on(t0);
        if (!e0) std::println("Dep Error: {}", to_string(e0.error()));
    }
    auto tmp = s.run();
    if (!tmp) std::println("Error: {}", to_string(tmp.error()));
}
