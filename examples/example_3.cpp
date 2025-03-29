#include <chrono>
#include <cstdlib>
#include <print>

#include "tpl.hpp"

using namespace tpl;

int main() {
    Scheduler s;
    {
        par::for_each</*Chunks=*/ 2>(s, range_t(0, 10, 1), [](range_t r) {
            std::string tmp;
            tmp += std::format("Running on: {}\n", ThisThread::get_native_id());
            for (auto i = r.start; i < r.end; i+= r.stride) {
                tmp += std::format("\tIter: {}\n", i); 
            }
            std::println("{}", tmp);
        });
        auto tmp = s.run();
        if (!tmp) std::println("Error: {}", to_string(tmp.error()));
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
        auto e0 = par::for_each</*Chunks=*/ 2>(s, range_t(0, 10, 1), t, [](range_t r, TaskToken& token) {
            auto offset = std::get<0>(token.arg<std::size_t>()).value_or(0).take();
            std::string tmp;

            tmp += std::format("Running on: {}, Offset: {}\n", ThisThread::get_native_id(), offset);
            for (auto i = r.start; i < r.end; i+= r.stride) {
                tmp += std::format("\tIter: {}\n", i + offset); 
            }
            std::println("{}", tmp);
        });

        if (!e0) {
            std::println("Error: {}", to_string(e0.error()));
            return 1;
        }
        auto tmp = s.run();
        if (!tmp) std::println("Error: {}", to_string(tmp.error()));
    }
}
