// tests/platform/input_action_map_test.cpp
//
// Contract tests for the ActionMap (F3.2, ADR-033): it binds/replaces/unbinds
// actions, resolves bindings in action id order, and rejects out-of-range
// actions, invalid sources and invalid codes. The InputQueue and input code
// vocabulary cases live in input_queue_test.cpp (rule 01: One File = One Task).
#include "infinity/platform/input.h"

#include "infinity/core/error.h"

#include <optional>

#include <doctest/doctest.h>

namespace {

// Errors are asserted by category, never by enum equality, so doctest never
// stringifies a PlatformError operand (ADL note in tests/core/error_test.cpp).
[[nodiscard]] bool isMappedTo(infinity::platform::PlatformError code,
                              infinity::core::ErrorCategory category) noexcept {
    return infinity::platform::categoryOf(code) == category;
}

} // namespace

TEST_CASE("ActionMap starts empty and binds an action") {
    infinity::platform::ActionMap actions;
    using infinity::platform::InputSource;

    CHECK(actions.boundCount() == 0);
    CHECK_FALSE(actions.resolve(InputSource::KEY, 0).has_value());

    const auto result = actions.bind(3, InputSource::KEY, 10);

    CHECK(result.has_value());
    CHECK(actions.boundCount() == 1);
    const std::optional<infinity::platform::ActionId> resolved =
        actions.resolve(InputSource::KEY, 10);
    CHECK(resolved.has_value());
    if (!resolved.has_value()) {
        return;
    }
    CHECK(*resolved == 3);
}

TEST_CASE("ActionMap resolves the lowest bound action id first") {
    infinity::platform::ActionMap actions;
    using infinity::platform::InputSource;

    const auto high = actions.bind(5, InputSource::KEY, 10);
    const auto low = actions.bind(2, InputSource::KEY, 10);

    CHECK(high.has_value());
    CHECK(low.has_value());
    CHECK(actions.boundCount() == 2);
    const std::optional<infinity::platform::ActionId> resolved =
        actions.resolve(InputSource::KEY, 10);
    CHECK(resolved.has_value());
    if (!resolved.has_value()) {
        return;
    }
    CHECK(*resolved == 2);
}

TEST_CASE("ActionMap bind replaces the previous binding on the same action") {
    infinity::platform::ActionMap actions;
    using infinity::platform::InputSource;

    const auto first = actions.bind(1, InputSource::KEY, 10);
    const auto second = actions.bind(1, InputSource::AXIS, 2);

    CHECK(first.has_value());
    CHECK(second.has_value());
    CHECK(actions.boundCount() == 1);
    CHECK_FALSE(actions.resolve(InputSource::KEY, 10).has_value());
    const std::optional<infinity::platform::ActionId> replaced =
        actions.resolve(InputSource::AXIS, 2);
    CHECK(replaced.has_value());
    if (!replaced.has_value()) {
        return;
    }
    CHECK(*replaced == 1);
}

TEST_CASE("ActionMap unbind removes the binding and is idempotent") {
    infinity::platform::ActionMap actions;
    using infinity::platform::InputSource;

    CHECK(actions.bind(4, InputSource::MOUSE_BUTTON, 0).has_value());
    CHECK(actions.boundCount() == 1);

    const auto removed = actions.unbind(4);

    CHECK(removed.has_value());
    CHECK(actions.boundCount() == 0);
    CHECK_FALSE(actions.resolve(InputSource::MOUSE_BUTTON, 0).has_value());

    const auto again = actions.unbind(4);
    CHECK(again.has_value());
    CHECK(actions.boundCount() == 0);
}

TEST_CASE("ActionMap rejects an out-of-range action id") {
    infinity::platform::ActionMap actions;
    using infinity::platform::InputSource;

    const auto bindResult = actions.bind(infinity::platform::MAX_ACTIONS, InputSource::KEY, 0);
    const auto unbindResult = actions.unbind(infinity::platform::MAX_ACTIONS);

    CHECK_FALSE(bindResult.has_value());
    CHECK(isMappedTo(bindResult.error(), infinity::core::ErrorCategory::INVALID_ARGUMENT));
    CHECK_FALSE(unbindResult.has_value());
    CHECK(isMappedTo(unbindResult.error(), infinity::core::ErrorCategory::INVALID_ARGUMENT));
    CHECK(actions.boundCount() == 0);
}

TEST_CASE("ActionMap rejects an invalid source or code on bind") {
    infinity::platform::ActionMap actions;
    using infinity::platform::InputSource;

    const auto badCode = actions.bind(0, InputSource::KEY, 256);
    CHECK_FALSE(badCode.has_value());
    CHECK(isMappedTo(badCode.error(), infinity::core::ErrorCategory::INVALID_ARGUMENT));
    CHECK(actions.boundCount() == 0);

    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto corrupted = static_cast<infinity::platform::InputSource>(0xEE);
    const auto badSource = actions.bind(0, corrupted, 0);
    CHECK_FALSE(badSource.has_value());
    CHECK(isMappedTo(badSource.error(), infinity::core::ErrorCategory::INVALID_ARGUMENT));
    CHECK(actions.boundCount() == 0);
}

TEST_CASE("ActionMap resolve returns nullopt for an unbound event") {
    infinity::platform::ActionMap actions;
    using infinity::platform::InputSource;

    CHECK(actions.bind(0, InputSource::KEY, 10).has_value());

    CHECK_FALSE(actions.resolve(InputSource::KEY, 11).has_value());
    CHECK_FALSE(actions.resolve(InputSource::AXIS, 10).has_value());
    CHECK_FALSE(actions.resolve(InputSource::MOUSE_BUTTON, 10).has_value());
}
