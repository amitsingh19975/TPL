#include <catch2/catch_test_macros.hpp>

#include <print>
#include "tpl/value_store.hpp"

using namespace tpl;

TEST_CASE("Value Store", "[value_store]" ) {
    GIVEN("An empty store with size 5") {
        auto store = ValueStore<5>{new BlockAllocator()};
        REQUIRE(store.empty());
        REQUIRE(store.size() == 0);

        WHEN("Ask for data at invalid index") {
            auto maybe = store.get<int>(0);
            REQUIRE(!maybe);
            REQUIRE(maybe.error() == ValueStoreError::not_found);
        }

        WHEN("Data is put inside the store") {
            store.put(0, std::string("data"));
            store.put(1, 10);
            store.put(2, 12.23);
            REQUIRE(store.size() == 3);
            REQUIRE(store.empty() == false);

            {
                auto val = store.get<std::string>(0);
                REQUIRE(val.has_value());
                REQUIRE(val.value().get() == "data");
                REQUIRE(store.size() == 3);
            }

            {
                auto val = store.consume<int>(1);
                REQUIRE(val.has_value());
                REQUIRE(val.value() == 10);
                REQUIRE(store.size() == 2);
            }

            {
                auto val = store.consume<int>(2);
                REQUIRE(!val.has_value());
                REQUIRE(store.size() == 2);
                REQUIRE(val.error() == ValueStoreError::type_mismatch);
            }

            store.remove(0);
            REQUIRE(store.size() == 1);
            store.clear();
            REQUIRE(store.size() == 0);
        }
    }
}
