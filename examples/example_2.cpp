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
    Schedular s;
    // task 1: computes sum upto 50
    auto t0 = s.add_task([]() -> std::size_t {
        auto sum = 0ul;
        for (auto i = 0ul; i < 50; ++i) {
            sum += i;
        }
        return sum;
    });

    // task 1: computes sum from 50 upto 101
    auto t1 = s.add_task([]() -> std::size_t {
        auto sum = 0ul;
        for (auto i = 50ul; i < 101ul; ++i) {
            sum += i;
        }
        return sum;
    });
    auto t2 = s.add_task([](TaskToken& token) {
        auto [lower, upper] = token.arg<std::size_t, std::size_t>();
        auto l = lower.value_or(0);
        auto r = upper.value_or(0);
        std::println("Lower: {}, Upper: {}", l, r);
        std::size_t sum = (100 * 101) / 2;
        if (sum != l + r) {
            std::println("Something went wrong");
        } else {
            std::println("Dependency is working");
        }
    });
    {
        //      t0      t1
        //       \      /
        //        \    /
        //         \  /
        //          t2
        auto e0 = t2.deps_on(t0, t1);
        if (!e0) std::println("Dep Error: {}", to_string(e0.error()));
    }
    auto tmp = s.run();
    if (!tmp) std::println("Error: {}", to_string(tmp.error()));
}
