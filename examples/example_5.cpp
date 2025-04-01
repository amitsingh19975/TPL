#include <cstdlib>
#include <ctime>
#include <print>
#include <stdexcept>

#include "tpl.hpp"

using namespace tpl;

int main() {
    Scheduler s;
    auto ts = s
        | []{
            srand(static_cast<unsigned>(time(0)));
            auto r = rand();
            if (r % 2 == 0) throw std::runtime_error(std::format("Random number({}) is even!", r));
            std::println("Task 0: {}", r);
        }
            + ErrorHandler([](std::exception const& e) {
                std::println("Error From Task 0: {}", e.what());
            })
        | []{ std::println("Task 1: {}", ThisThread::pool_id()); }
        | TaskGroup {
           []{ return std::make_pair(0, ThisThread::pool_id()); },
           []{ return std::make_pair(1, ThisThread::pool_id()); },
           []{ return std::make_pair(2, ThisThread::pool_id()); },
        } > [](TaskToken& t){
            auto args = t.all_of<std::pair<int, std::size_t>>();
            if (args.size() != 3) throw std::runtime_error("Args must be 3");

            for (auto i = 0ul; i < args.size(); ++i) {
                auto [a, b] = args[i].take();
                std::println("Pair[{}]: {}", a, b);
            } 
        } + ErrorHandler([](std::exception const& e) {
            std::println("Error thrown when args is not 3: {}", e.what());
        })
        ;

    ts.run();
}
