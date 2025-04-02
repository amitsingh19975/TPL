#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <print>

#include "tpl/list.hpp"

using namespace tpl;

TEST_CASE("Block Sized List", "[block_sized_list]" ) {
    WHEN("An empty list is constructed") {
        auto l = BlockSizedList<int, /*BlockSize=*/8>{};
        REQUIRE(l.size() == 0);
        REQUIRE(l.nblocks() == 0);
        REQUIRE(l.empty() == true);
    }

    GIVEN("A block sized list with block size of 8") {
        auto l = BlockSizedList<int, /*BlockSize=*/8>{};

        WHEN("Pushing a single element") {
            l.push_back(10);
            REQUIRE(l.size() == 1);
            REQUIRE(l.nblocks() == 1);
            REQUIRE(!l.empty());
            REQUIRE(l[0] == 10);
        }

        WHEN("Filling a single block") {
            for (auto i = 0ul; i < l.block_size; ++i) {
                l.push_back(i);
                REQUIRE(l.size() == i + 1);
                REQUIRE(l.nblocks() == 1);
                REQUIRE(l[i] == i);
            }
            REQUIRE(!l.empty());
        }

        WHEN("Pushing one more element after filled block") {
            for (auto i = 0ul; i < l.block_size; ++i) {
                l.push_back(i);
                REQUIRE(l.size() == i + 1);
                REQUIRE(l.nblocks() == 1);
                REQUIRE(l[i] == i);
            }
            l.push_back(11);
            REQUIRE(l.size() == l.block_size + 1);
            REQUIRE(l.nblocks() == 2);
            REQUIRE(l[l.block_size] == 11);
        }

        WHEN("Inserting beyond cached nodes") {
            for (auto i = 0ul; i < l.block_size * 1000; ++i) {
                l.push_back(i);
                auto blocks = ((i + 1) + l.block_size - 1) / l.block_size;
                REQUIRE(l.size() == i + 1);
                REQUIRE(l.nblocks() == blocks);
                REQUIRE(l[i] == i);
            }

            l.clear();
            REQUIRE(l.size() == 0);
            REQUIRE(l.nblocks() == 0);
            REQUIRE(l.empty() == true);
        }
    }
}
