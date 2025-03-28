#include <chrono>
#include <cstdlib>
#include <print>

#include "tpl.hpp"

using namespace tpl;

int main() {
    Scheduler s;
    {
        auto res = par::reduce</*Chunks=*/ 2>(s, range_t(0, 10, 1), [](range_t r) -> std::size_t {
            std::size_t res {};
            for (auto i = r.start; i < r.end; i+= r.stride) {
                res += i;
            }
            return res;
        }, std::size_t{});
        if (!res) {
            std::println("Error: {}", to_string(res.error()));
            return 1;
        }
        auto tmp = s.run();
        if (!tmp) std::println("Error: {}", to_string(tmp.error()));
        auto value = res->get();
        std::println("Res: {} == {}", ((9 * 10) / 2), value);
    }

    s.reset();

    {
        auto t = s.add_task([] () -> std::size_t {
            for (auto i = 0ul; i < 4; ++i) {
                std::println("Doing work... {}sec", i);
                ThisThread::sleep_for(std::chrono::seconds(1));
            }
            return 10;
        });
        auto e0 = par::reduce</*Chunks=*/ 2>(s, range_t(0, 10, 1), t, [](range_t r, TaskToken& token) {
            auto offset = std::get<0>(token.arg<std::size_t>()).value_or(0);
            std::size_t res {};
            for (auto i = r.start; i < r.end; i+= r.stride) {
                res += i;
            }
            return res + offset;
        }, std::size_t{});

        if (!e0) {
            std::println("Error: {}", to_string(e0.error()));
            return 1;
        }
        auto tmp = s.run();
        auto value = e0->get();
        std::println("Res: {} == {}", ((9 * 10) / 2 + 10 * 5), value);
        if (!tmp) std::println("Error: {}", to_string(tmp.error()));
    }
}
