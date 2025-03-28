#include <cstdlib>
#include <print>

#include "tpl.hpp"
#include "tpl/schedular.hpp"

using namespace tpl;

struct Test {
    Test() = default;
    Test(Test const&) {
        std::println("Copying");
    }
    Test(Test &&) {
        std::println("Moving");
    }
    Test& operator=(Test const&) = default;
    Test& operator=(Test &&) = delete;
    ~Test() {
        std::println("Called ~Test()");
    }
};

template <typename T, typename E>
void print(std::expected<T, E> const& v) {
    if (v) std::println("Value: {}", *v);
    else std::println("Error: {}", to_string(v.error()));
}

int main() {
    Schedular s;
    [[maybe_unused]] auto t0 = s.add_task([](TaskToken& token) {
        auto value = token.arg<int>(TaskId(1)).value_or(-1);
        std::println("Hello from task 0: Called after => {}", value);
        token.stop();
    });
    auto i = 0ul;
    [[maybe_unused]] auto t1 = s.add_task([&i](TaskToken& token) {
        std::println("[{}]: Hello from task 1", i);
        if (i ++ < 5) {
            token.schedule();
        }
        return 1;
    });
    [[maybe_unused]] auto t2 = s.add_task([](TaskToken& token) {
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
