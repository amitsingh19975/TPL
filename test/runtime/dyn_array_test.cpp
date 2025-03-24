#include <catch2/catch_test_macros.hpp>
#include <print>
#include <vector>

#include "tpl/dyn_array.hpp"

using namespace tpl;

TEST_CASE("Dynamic Array", "[dyn_array]" ) {
    GIVEN("A empty constructed array") {
        auto a = DynArray<int>{};
        REQUIRE(a.size() == 0);
        REQUIRE(a.empty() == true);
        REQUIRE(a.data() == nullptr);
        REQUIRE(a.alloc() == AllocatorManager::instance().get_alloc());
    }

    GIVEN("A newly constructed array with size and default value") {
        auto a = DynArray<int>(10, -1);
        REQUIRE(a.size() == 10);
        REQUIRE(a.empty() == false);
        REQUIRE(a.data() != nullptr);
        REQUIRE(a.alloc() == AllocatorManager::instance().get_alloc());

        for (auto i = 0ul; i < a.size(); ++i) {
            INFO(std::format("[{}]: {} == {}", i, a[i], -1));
            REQUIRE(a[i] == -1); 
        }

        WHEN("Pushed and poped back") {
            a.push_back(11);
            REQUIRE(a.size() == 11);
            REQUIRE(a[10] == 11);
            REQUIRE(a.back() == 11);
            REQUIRE(a.front() == -1);

            REQUIRE(a.pop_back() == 11);
            REQUIRE(a.size() == 10);
            REQUIRE(a.back() == -1);
            REQUIRE(a.front() == -1);
        }

        WHEN("Emplaced back") {
            a.emplace_back(11);
            REQUIRE(a.size() == 11);
            REQUIRE(a[10] == 11);
            REQUIRE(a.back() == 11);
            REQUIRE(a.front() == -1);
        }

        WHEN("Resized to larger size") {
            a.resize(20, 2);
            REQUIRE(a.size() == 20);
            for (auto i = 10ul; i < a.size(); ++i) {
                INFO(std::format("[{}]: {} == {}", i, a[i], -1));
                REQUIRE(a[i] == 2); 
            }
        }

        WHEN("Resized to smaller size") {
            a.resize(5);
            REQUIRE(a.size() == 5);
        }

        WHEN("Reserve") {
            REQUIRE(a.capacity() == 10);
            a.reserve(100);
            REQUIRE(a.size() == 10);
            REQUIRE(a.capacity() == 100);
        }

        WHEN("Copyed using constructor") {
            std::iota(a.begin(), a.end(), 0);
            DynArray<int> c(a);
            REQUIRE(c.data() != a.data());
            REQUIRE(c.size() == a.size());

            for (auto i = 0ul; i < a.size(); ++i) {
                INFO(std::format("[{}]: {} == {}", i, a[i], i));
                REQUIRE(a[i] == i); 
            }
        }

        WHEN("Copyed using assignment operator") {
            std::iota(a.begin(), a.end(), 0);
            DynArray<int> c;
            c = a;
            REQUIRE(c.data() != a.data());
            REQUIRE(c.size() == a.size());

            for (auto i = 0ul; i < a.size(); ++i) {
                INFO(std::format("[{}]: {} == {}", i, c[i], i));
                REQUIRE(c[i] == i); 
            }
        }

        WHEN("Moved using constructor") {
            std::iota(a.begin(), a.end(), 0);
            auto old_ptr = a.data();
            DynArray<int> c(std::move(a));
            REQUIRE(a.data() == nullptr);
            REQUIRE(c.data() == old_ptr);
            REQUIRE(c.size() == 10);

            for (auto i = 0ul; i < a.size(); ++i) {
                INFO(std::format("[{}]: {} == {}", i, c[i], i));
                REQUIRE(c[i] == i); 
            }
        }

        WHEN("Moved using assignment operator") {
            std::iota(a.begin(), a.end(), 0);
            auto old_ptr = a.data();
            DynArray<int> c;
            c = std::move(a);
            REQUIRE(a.data() == nullptr);
            REQUIRE(c.data() == old_ptr);
            REQUIRE(c.size() == 10);

            for (auto i = 0ul; i < a.size(); ++i) {
                INFO(std::format("[{}]: {} == {}", i, c[i], i));
                REQUIRE(c[i] == i); 
            }
        }

        WHEN("Finding an element using linear search") {
            std::iota(a.begin(), a.end(), 0);
            for (auto i = 0ul; i < a.size(); ++i) {
                auto e0 = a.find(i);
                REQUIRE(e0 != a.end());
                REQUIRE(*e0 == i);
                REQUIRE(std::distance(a.begin(), e0) == i);
            }
        }

        WHEN("Finding an element using binary search") {
            std::iota(a.begin(), a.end(), 0);
            for (auto i = 0ul; i < a.size(); ++i) {
                auto e0 = a.binary_search(i);
                REQUIRE(e0 != a.end());
                REQUIRE(*e0 == i);
                REQUIRE(std::distance(a.begin(), e0) == i);
            }
        }

        WHEN("Erasing element at a position") {
            std::iota(a.begin(), a.end(), 0);
            REQUIRE(a.erase(5) == 5);
            REQUIRE(a.size() == 9);
            REQUIRE(a[5] == 6);
        }

        WHEN("Erasing element at a position with size") {
            std::iota(a.begin(), a.end(), 0);
            a.erase(2, 3);
            REQUIRE(a.size() == 10 - 3);
            REQUIRE(a[2] == 5);
            REQUIRE(a[3] == 6);
            REQUIRE(a[4] == 7);
        }

        WHEN("Erasing element using iterators") {
            std::iota(a.begin(), a.end(), 0);
            a.erase(a.begin() + 2, a.begin() + 5);
            REQUIRE(a.size() == 10 - 3);
            REQUIRE(a[2] == 5);
            REQUIRE(a[3] == 6);
            REQUIRE(a[4] == 7);
        }

        WHEN("Insert at a position") {
            a.insert(0, 10);
            REQUIRE(a[0] == 10);
            REQUIRE(a.size() == 11);

            a.insert(5, 15);
            REQUIRE(a[5] == 15);
            REQUIRE(a.size() == 12);

            a.insert(a.size(), 20);
            REQUIRE(a.back() == 20);
            REQUIRE(a.size() == 13);
        }

        WHEN("Insert at the start using iterators") {
            std::vector<int> vs = { 1, 2, 3, 4 };
            a.insert(a.begin(), vs.begin(), vs.end());
            REQUIRE(a[0] == 1);
            REQUIRE(a[1] == 2);
            REQUIRE(a[2] == 3);
            REQUIRE(a[3] == 4);
            REQUIRE(a.size() == 10 + vs.size());
        }

        WHEN("Insert in the middle using iterators") {
            std::vector<int> vs = { 1, 2, 3, 4 };
            a.insert(a.begin() + 5, vs.begin(), vs.end());
            REQUIRE(a[0 + 5] == 1);
            REQUIRE(a[1 + 5] == 2);
            REQUIRE(a[2 + 5] == 3);
            REQUIRE(a[3 + 5] == 4);
            REQUIRE(a.size() == 10 + vs.size());
        }

        WHEN("Insert at the end using iterators") {
            std::vector<int> vs = { 1, 2, 3, 4 };
            a.insert(a.end(), vs.begin(), vs.end());
            REQUIRE(a[9] == -1);
            REQUIRE(a[10 + 0] == 1);
            REQUIRE(a[10 + 1] == 2);
            REQUIRE(a[10 + 2] == 3);
            REQUIRE(a[10 + 3] == 4);
            REQUIRE(a.size() == 10 + vs.size());
        }
    }

    WHEN("A newly constructed using initializer list") {
        DynArray<int> a {1, 2, 3, 4, 5, 6};
        REQUIRE(a.size() == 6);
        REQUIRE(a.capacity() == 6);
        for (auto i = 0ul; i < a.size(); ++i) {
            REQUIRE(a[i] == i + 1);
        }
    }

    WHEN("A newly constructed using iterators") {
        std::vector<int> vs {1, 2, 3, 4, 5, 6};
        DynArray<int> a(vs.begin(), vs.end());
        REQUIRE(a.size() == 6);
        REQUIRE(a.capacity() == 6);
        for (auto i = 0ul; i < a.size(); ++i) {
            REQUIRE(a[i] == i + 1);
        }
    }
}

