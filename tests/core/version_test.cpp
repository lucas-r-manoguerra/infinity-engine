// tests/core/version_test.cpp
#include "infinity/core/version.h"

#include <string_view>

#include <doctest/doctest.h>

TEST_CASE("infinity::core::version returns the engine semantic version") {
    const std::string_view expected = "0.1.0";
    CHECK(infinity::core::version() == expected);
}
