#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include "../src/main.cpp"  // Include your SoA code

TEST_CASE("DocumentStore adds documents correctly", "[soa]") {
    DocumentStore store;
    add_document(store, 1, 1672531200, "Test Title", "Test Content");
    REQUIRE(store.ids[0] == 1);
    REQUIRE(store.timestamps[0] == 1672531200);
    REQUIRE(store.titles[0] == "Test Title");
    REQUIRE(store.contents[0] == "Test Content");
}