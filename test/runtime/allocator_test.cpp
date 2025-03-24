#include <catch2/catch_test_macros.hpp>
#include <print>

#include "tpl/allocator.hpp"

using namespace tpl;

TEST_CASE("Bump Allocator", "[allocator][bump]" ) {
    GIVEN("Allocator of size 20 Bytes") {
        BumpAllocator alloc(20);
        REQUIRE(alloc.size() == 20);
        REQUIRE(alloc.free_space() == 20);
        REQUIRE(alloc.is_owned());

        WHEN("Over-Allocating") {
            auto p0 = alloc.alloc<int>();
            REQUIRE(p0 != nullptr);
            REQUIRE(alloc.free_space() == 16);

            auto p1 = alloc.alloc<int>(4);
            REQUIRE(p1 != nullptr);
            REQUIRE(alloc.free_space() == 0);

            auto p2 = alloc.alloc<int>();
            REQUIRE(p2 == nullptr);
            REQUIRE(alloc.free_space() == 0);
        }

        WHEN("Reseting Allocator") {
            auto p0 = alloc.alloc<int>();
            REQUIRE(p0 != nullptr);
            REQUIRE(alloc.free_space() == 16);

            alloc.reset();
            REQUIRE(alloc.free_space() == 20);
        }

        WHEN("Aligned Allocation") {
            auto p0 = alloc.alloc<char>(1);
            REQUIRE(p0 != nullptr);
            REQUIRE(alloc.free_space() == 19);

            auto p1 = alloc.alloc<int>(1, 8);
            REQUIRE(p1 != nullptr);
            auto addr = reinterpret_cast<std::intptr_t>(p1);
            REQUIRE(addr % 8 == 0);
            REQUIRE(alloc.free_space() <= 11);

            alloc.reset();
        }

        WHEN("Checkpointing") {
            auto p0 = alloc.alloc<char>(1);
            REQUIRE(p0 != nullptr);
            REQUIRE(alloc.free_space() == 19);

            auto ch = alloc.marker();
            REQUIRE(ch.second == 1);
            REQUIRE(ch.first == 1);

            auto p1 = alloc.alloc<char>(4);
            REQUIRE(p1 != nullptr);
            REQUIRE(alloc.free_space() == 15);

            auto ch2 = alloc.marker();
            REQUIRE(ch2.second == 5);
            REQUIRE(ch2.first == 2);

            alloc.set_marker(ch);
            auto ch3 = alloc.marker();
            REQUIRE(ch3.second == 1);
            REQUIRE(ch3.first == 1);
            REQUIRE(alloc.free_space() == 19);
        }
    }
}

TEST_CASE("Block Allocator", "[allocator][block]" ) {
    GIVEN("Empty Allocator") {
        BlockAllocator alloc;
        REQUIRE(alloc.empty());

        WHEN("Trying to exhaust") {
            auto p0 = alloc.alloc<int>(1);
            REQUIRE(p0 != nullptr);
            REQUIRE(alloc.nblocks() == 1);

            auto p1 = alloc.alloc<char>(alloc.back()->free_space());
            REQUIRE(p1 != nullptr);
            REQUIRE(alloc.nblocks() == 1);

            auto p2 = alloc.alloc<int>(1);
            REQUIRE(p2 != nullptr);
            REQUIRE(alloc.nblocks() == 2);
        }

        WHEN("Reseting allocator with reuse") {
            auto p0 = alloc.alloc<int>(1);
            REQUIRE(p0 != nullptr);
            REQUIRE(alloc.nblocks() == 1);

            auto p1 = alloc.alloc<char>(alloc.back()->free_space());
            REQUIRE(p1 != nullptr);
            REQUIRE(alloc.nblocks() == 1);

            auto p2 = alloc.alloc<int>(1);
            REQUIRE(p2 != nullptr);
            REQUIRE(alloc.nblocks() == 2);

            alloc.reset();
            REQUIRE(alloc.nblocks() == 2);
        }

        WHEN("Reseting allocator with no reuse") {
            auto p0 = alloc.alloc<int>(1);
            REQUIRE(p0 != nullptr);
            REQUIRE(alloc.nblocks() == 1);

            auto p1 = alloc.alloc<char>(alloc.back()->free_space());
            REQUIRE(p1 != nullptr);
            REQUIRE(alloc.nblocks() == 1);

            auto p2 = alloc.alloc<int>(1);
            REQUIRE(p2 != nullptr);
            REQUIRE(alloc.nblocks() == 2);

            alloc.reset(false);
            REQUIRE(alloc.nblocks() == 0);
        }

        WHEN("Checkpointing") {
            auto p0 = alloc.alloc<int>(1);
            REQUIRE(p0 != nullptr);
            REQUIRE(alloc.nblocks() == 1);

            auto p1 = alloc.alloc<char>(alloc.front()->free_space());
            REQUIRE(p1 != nullptr);
            REQUIRE(alloc.nblocks() == 1);

            auto p2 = alloc.alloc<int>(100);
            REQUIRE(p2 != nullptr);
            REQUIRE(alloc.nblocks() == 2);

            auto c0 = alloc.marker();
            auto c0_size = alloc.front()->free_space();
            REQUIRE(&c0.alloc->bm == alloc.front());

            auto p3 = alloc.alloc<char>(alloc.front()->free_space());
            REQUIRE(p3 != nullptr);
            REQUIRE(alloc.nblocks() == 2);
            REQUIRE(alloc.front()->free_space() == 0);

            auto p4 = alloc.alloc<char>(10);
            REQUIRE(p4 != nullptr);
            REQUIRE(alloc.nblocks() == 3);

            alloc.set_marker(c0);
            auto c1 = alloc.marker();
            REQUIRE(alloc.nblocks() == 2);
            REQUIRE(&c1.alloc->bm == alloc.front());
            auto* block = alloc.front();
            REQUIRE(block->free_space() == c0_size);
        }
    }
}

TEST_CASE("Allocator Manager", "[allocator][manager]" ) {
    auto& mg = AllocatorManager::instance(); 
    REQUIRE(mg.get_alloc() == AllocatorManager::get_global_alloc());

    auto tmp = BlockAllocator("Temp");
    mg.swap(&tmp);
    REQUIRE(mg.get_alloc() == &tmp);

    mg.reset();
    REQUIRE(mg.get_alloc() == AllocatorManager::get_global_alloc());
}
