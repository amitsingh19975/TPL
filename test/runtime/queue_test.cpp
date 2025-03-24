#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <print>
#include <thread>

#include "tpl/queue.hpp"

using namespace tpl;

TEST_CASE("Cicular Queue", "[queue][circular_queue]" ) {
    using namespace internal; 
    WHEN("A empty circular constructed") {
        auto q = CircularQueue<int, 32>{};
        REQUIRE(q.size() == 0);
        REQUIRE(q.empty() == true);
        REQUIRE(q.full() == false);
        q.clear();
    }

    GIVEN("A circular queue") {
        auto q = CircularQueue<int, 16>{};
        WHEN("Run on single thread") {
            for (auto i = 0ul; i < 32; ++i) {
                REQUIRE(q.push_value(i) == true);
                auto item = q.pop();
                REQUIRE(item.has_value());
                INFO(std::format("[{}]: {} == {}", i, *item, i));
                REQUIRE(*item == i);
            }
        }

        WHEN("Run on two thread") {
            std::array<bool, 100> flags{false};
            std::atomic<int> finshed{0};

            auto fn = [&q, &finshed](int start, int step) {
                for (int num = start; num < 100;) {
                    if (q.push_value(num)) {
                        num += step;
                        continue;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 100));
                }
                finshed.fetch_add(1);
            };
            auto t1 = std::thread(fn, 0, 2);
            auto t2 = std::thread(fn, 1, 2);

            int tests = 1000;
            while (tests-- > 0 && (finshed.load() < 2 || !q.empty())) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                while (!q.empty()) {
                    auto idx = q.pop();
                    if (!idx) {
                        continue;
                    }
                    auto& tmp = flags.at(*idx);
                    tmp = !tmp;
                }
            }

            for (auto i = 0ul; i < flags.size(); ++i) {
                INFO(std::format("[{}]: {} == true", i, flags[i]));
                REQUIRE(flags[i] == true);
            }
            t1.join();
            t2.join();
        }
    }
}

TEST_CASE("Thread Safe Queue", "[queue]" ) {
    WHEN("A empty queue is constructed") {
        auto q = Queue<int>{};
        REQUIRE(q.nodes() == 0);
        REQUIRE(q.empty() == true);

        q.push(10);
        REQUIRE(q.nodes() == 1);
        REQUIRE(q.empty() == false);
    }

    GIVEN("A queue") {
        auto q = Queue<std::pair<std::size_t, int>>{};
        REQUIRE(q.nodes() == 0);
        REQUIRE(q.empty() == true);
        std::atomic<int> finshed{0};

        auto fn = [&q, &finshed](std::size_t id, int size) {
            for (auto i = 0; i < size; ++i) {
                q.emplace(id, i);
                std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 10));
            }
            finshed.fetch_add(1);
        };
        static constexpr auto N = 10ul;
        static constexpr auto M = 500;

        bool flags[N][M] = {};
        std::vector<std::thread> ts;

        for (auto i = 0ul; i < N; ++i) {
            ts.emplace_back(fn, i, M);
        }

        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            if ((finshed.load() == ts.size()) && q.empty()) break;
            auto count = 0ul;
            while (!q.empty()) {
                auto tmp = q.pop();
                if (!tmp) break;
                auto [id, i] = *tmp;
                flags[id][i] = true;
                ++count;
            }
        }

        for (auto i = 0ul; i < N; ++i) {
            for (auto j = 0; j < M; ++j) {
                INFO(std::format("[(id: {}, i: {})]: {} == true", i, j, flags[i][j]));
                REQUIRE(flags[i][j] == true);
            }
        }

        REQUIRE(q.nodes() == 1);
        REQUIRE(q.empty() == true);

        for (auto& t: ts) t.join();
    }
}
