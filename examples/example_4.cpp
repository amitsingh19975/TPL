#include <chrono>
#include <cstdlib>
#include <print>

#include "tpl.hpp"

using namespace tpl;

int main() {
    Scheduler s;
    {
        std::vector<std::size_t> v(100);
        std::iota(v.begin(), v.end(), 1);

        auto res = par::reduce</*Chunks=*/ 2>(s, v.begin(), v.end(), std::size_t{}, std::plus<>{});
        if (!res) {
            std::println("Error: {}", to_string(res.error()));
            return 1;
        }
        auto tmp = s.run();
        if (!tmp) std::println("Error: {}", to_string(tmp.error()));
        auto value = s.get_result<std::size_t>(*res).value_or(0);
        std::println("Res: {} == {}", ((101 * 100) / 2), value);
    }

    s.reset();

    {
        std::vector<std::size_t> v(10);
        std::iota(v.begin(), v.end(), 1);

        auto t = s.add_task([&v] () -> std::size_t {
            for (auto i = 0ul; i < 4; ++i) {
                std::println("Starting work on array with {} elements... {}sec", v.size(), i);
                ThisThread::sleep_for(std::chrono::seconds(1));
            }
            return 10;
        });
        auto e0 = par::reduce</*Chunks=*/ 2>(s, v.begin(), v.end(), t, std::size_t{}, [](auto acc, auto v, TaskToken* t) {
            std::size_t offset{};
            if (t) {
                offset = std::get<0>(t->arg<std::size_t>()).value_or(0).take();
            }
            return acc + v + offset;
        });

        if (!e0) {
            std::println("Error: {}", to_string(e0.error()));
            return 1;
        }

        auto tmp = s.run();
        auto value = s.get_result<std::size_t>(*e0).value_or(0);
        auto total = (10 * 11) / 2;
        std::println("Res: {} == {}", (total + (10 / 2) * 10 * 2), value);
        if (!tmp) std::println("Error: {}", to_string(tmp.error()));
    }
}
