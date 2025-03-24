#include <catch2/catch_test_macros.hpp>

#include <print>
#include "tpl/signal_tree/tree.hpp"

using namespace tpl;

TEST_CASE("Signal Tree", "[signal_tree]" ) {
    GIVEN("An empty tree") {
        auto tree = SignalTree<4ul>{};
        REQUIRE(tree.empty());
        REQUIRE(tree.data().size() == 1);
        REQUIRE(tree.capacity == 4);

        WHEN("A leaf is set") {
            for (auto i = 0ul; i < tree.capacity; ++i) {
                INFO(std::format("Tree[{}]", i));
                REQUIRE(tree.get_level<2>().get_value({i}) == 0);
                tree.set(i);
                REQUIRE(tree.get_level<2>().get_value({i}) == 1);
            }
            REQUIRE(tree.get_level<0>().get_value({0}) == 4);
            REQUIRE(tree.get_level<1>().get_value({0}) == 2);
            REQUIRE(tree.get_level<1>().get_value({1}) == 2);
        }

        WHEN("A leaf node is selected") {
            tree.set(0);
            tree.set(2);
            tree.set(1);
            REQUIRE(tree.get_level<0>().get_value({0}) == 3);
            REQUIRE(tree.get_level<1>().get_value({0}) == 2);
            REQUIRE(tree.get_level<1>().get_value({1}) == 1);

            // Selection happens from left to right
            {
                auto [idx, zero] = tree.select();
                REQUIRE(idx.index == 0);
                REQUIRE(zero == true);
                REQUIRE(tree.get_level<0>().get_value({0}) == 2);
                REQUIRE(tree.get_level<1>().get_value({0}) == 1);
                REQUIRE(tree.get_level<1>().get_value({1}) == 1);
            }
            {
                auto [idx, zero] = tree.select();
                REQUIRE(idx.index == 1);
                REQUIRE(zero == true);
                REQUIRE(tree.get_level<0>().get_value({0}) == 1);
                REQUIRE(tree.get_level<1>().get_value({0}) == 0);
                REQUIRE(tree.get_level<1>().get_value({1}) == 1);
            }
            {
                auto [idx, zero] = tree.select();
                REQUIRE(idx.index == 2);
                REQUIRE(zero == true);
                REQUIRE(tree.get_level<0>().get_value({0}) == 0);
                REQUIRE(tree.get_level<1>().get_value({0}) == 0);
                REQUIRE(tree.get_level<1>().get_value({1}) == 0);
            }
            REQUIRE(tree.empty());
        }
    }
}
