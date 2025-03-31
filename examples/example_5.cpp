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
           []{ return std::make_pair(0, ThisThread::pool_id()); },
           []{ return std::make_pair(1, ThisThread::pool_id()); },
           []{ return std::make_pair(2, ThisThread::pool_id()); },
        } > [](TaskToken& t){
            auto args = t.all_of<std::pair<int, std::size_t>>();
            assert(args.size() == 3);

            for (auto i = 0ul; i < args.size(); ++i) {
                auto [a, b] = args[i].take();
                std::println("Pair[{}]: {}", a, b);
            }
        }
        ;

    ts.run();
}
