// tests/platform/input_test.cpp
//
// Contract tests for the deterministic input layer (F3.2, ADR-033): the
// InputQueue preserves order and assigns sequence numbers, rejects malformed
// events, drops instead of failing on overflow (visible via wasOverflowed)
// and resets cleanly; the ActionMap binds/replaces/unbinds/resolves in action
// id order and rejects out-of-range actions. codeValid (input_source.h)
// coverage also lives here, the source vocabulary belongs to the input domain.
#include "infinity/platform/input.h"

#include "infinity/core/error.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include <doctest/doctest.h>

namespace {

// Errors are asserted by category, never by enum equality, so doctest never
// stringifies a PlatformError operand (ADL note in tests/core/error_test.cpp).
[[nodiscard]] bool isMappedTo(infinity::platform::PlatformError code,
                              infinity::core::ErrorCategory category) noexcept {
    return infinity::platform::categoryOf(code) == category;
}

} // namespace

TEST_CASE("codeValid accepts codes inside each source's range") {
    using infinity::platform::InputSource;
    CHECK(infinity::platform::codeValid(InputSource::KEY, 0));
    CHECK(infinity::platform::codeValid(InputSource::KEY, 255));
    CHECK(infinity::platform::codeValid(InputSource::MOUSE_BUTTON, 0));
    CHECK(infinity::platform::codeValid(InputSource::MOUSE_BUTTON, 7));
    CHECK(infinity::platform::codeValid(InputSource::GAMEPAD_BUTTON, 15));
    CHECK(infinity::platform::codeValid(InputSource::AXIS, 0));
    CHECK(infinity::platform::codeValid(InputSource::AXIS, 15));
}

TEST_CASE("codeValid rejects codes outside each source's range") {
    using infinity::platform::InputSource;
    CHECK_FALSE(infinity::platform::codeValid(InputSource::KEY, 256));
    CHECK_FALSE(infinity::platform::codeValid(InputSource::MOUSE_BUTTON, 8));
    CHECK_FALSE(infinity::platform::codeValid(InputSource::GAMEPAD_BUTTON, 16));
    CHECK_FALSE(infinity::platform::codeValid(InputSource::AXIS, 16));
}

TEST_CASE("codeValid rejects a corrupted source value") {
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto corrupted = static_cast<infinity::platform::InputSource>(0xEE);
    CHECK_FALSE(infinity::platform::codeValid(corrupted, 0));
}

TEST_CASE("InputQueue returns nullopt on an empty queue") {
    infinity::platform::InputQueue queue;
    CHECK_FALSE(queue.tryPop().has_value());
    CHECK(queue.size() == 0);
}

TEST_CASE("InputQueue preserves order and assigns increasing sequence numbers") {
    infinity::platform::InputQueue queue;
    using infinity::platform::InputSource;

    const auto a = queue.push(InputSource::KEY, 1, 1.0f);
    const auto b = queue.push(InputSource::AXIS, 0, 0.5f);
    const auto c = queue.push(InputSource::MOUSE_BUTTON, 0, 0.0f);

    CHECK(a.has_value());
    CHECK(b.has_value());
    CHECK(c.has_value());
    CHECK(queue.size() == 3);

    const std::optional<infinity::platform::InputEvent> first = queue.tryPop();
    const std::optional<infinity::platform::InputEvent> second = queue.tryPop();
    const std::optional<infinity::platform::InputEvent> third = queue.tryPop();

    CHECK(first.has_value());
    CHECK(second.has_value());
    CHECK(third.has_value());
    if (!first.has_value() || !second.has_value() || !third.has_value()) {
        return;
    }
    CHECK(first->sequence == 1);
    CHECK(second->sequence == 2);
    CHECK(third->sequence == 3);
    CHECK(first->source == InputSource::KEY);
    CHECK(first->code == 1);
    CHECK(second->source == InputSource::AXIS);
    CHECK(second->code == 0);
    CHECK(third->source == InputSource::MOUSE_BUTTON);
    CHECK(third->code == 0);
    CHECK(queue.size() == 0);
}

TEST_CASE("InputQueue rejects an invalid code") {
    infinity::platform::InputQueue queue;
    using infinity::platform::InputSource;

    const auto result = queue.push(InputSource::KEY, 256, 1.0f);

    CHECK_FALSE(result.has_value());
    CHECK(isMappedTo(result.error(), infinity::core::ErrorCategory::INVALID_ARGUMENT));
    CHECK(queue.size() == 0);
}

TEST_CASE("InputQueue rejects a corrupted source") {
    infinity::platform::InputQueue queue;

    // Deliberate out-of-range value: exercises the totality contract of the
    // validation (error, never UB).
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto corrupted = static_cast<infinity::platform::InputSource>(0xEE);
    const auto result = queue.push(corrupted, 0, 0.0f);

    CHECK_FALSE(result.has_value());
    CHECK(isMappedTo(result.error(), infinity::core::ErrorCategory::INVALID_ARGUMENT));
    CHECK(queue.size() == 0);
}

TEST_CASE("a full InputQueue drops the event but keeps reporting success") {
    infinity::platform::InputQueue queue;
    using infinity::platform::InputSource;

    for (std::size_t i = 0; i < infinity::platform::InputQueue::capacity(); ++i) {
        const auto pushed = queue.push(InputSource::KEY, static_cast<std::uint16_t>(i), 1.0f);
        CHECK(pushed.has_value());
    }
    CHECK_FALSE(queue.wasOverflowed());
    CHECK(queue.size() == infinity::platform::InputQueue::capacity());

    const auto dropped = queue.push(InputSource::KEY, 0, 1.0f);

    CHECK(dropped.has_value());
    CHECK(queue.wasOverflowed());
    CHECK(queue.size() == infinity::platform::InputQueue::capacity());
}

TEST_CASE("an overflowed queue drains the retained events and latches the flag until clear") {
    infinity::platform::InputQueue queue;
    using infinity::platform::InputSource;

    for (std::size_t i = 0; i < infinity::platform::InputQueue::capacity(); ++i) {
        const auto pushed = queue.push(InputSource::KEY, static_cast<std::uint16_t>(i), 1.0f);
        CHECK(pushed.has_value());
    }
    const auto overflowed = queue.push(InputSource::KEY, 0, 1.0f);
    CHECK(overflowed.has_value());

    for (std::size_t i = 0; i < infinity::platform::InputQueue::capacity(); ++i) {
        const std::optional<infinity::platform::InputEvent> event = queue.tryPop();
        CHECK(event.has_value());
        if (!event.has_value()) {
            return;
        }
        CHECK(event->sequence == i + 1);
        CHECK(event->code == i);
    }
    CHECK_FALSE(queue.tryPop().has_value());
    // The loss flag is a latch: it stays set after draining so the runtime can
    // observe the loss and acknowledge it with clear() (documented in input.h).
    CHECK(queue.wasOverflowed());

    queue.clear();
    CHECK_FALSE(queue.wasOverflowed());
}

TEST_CASE("clear resets the queue, its sequence and its overflow flag") {
    infinity::platform::InputQueue queue;
    using infinity::platform::InputSource;

    const auto seed = queue.push(InputSource::KEY, 0, 1.0f);
    CHECK(seed.has_value());
    for (std::size_t i = 0; i < infinity::platform::InputQueue::capacity(); ++i) {
        const auto pushed = queue.push(InputSource::KEY, 0, 1.0f);
        CHECK(pushed.has_value());
    }
    CHECK(queue.wasOverflowed());

    queue.clear();

    CHECK(queue.size() == 0);
    CHECK_FALSE(queue.wasOverflowed());
    CHECK_FALSE(queue.tryPop().has_value());

    const auto first = queue.push(InputSource::KEY, 0, 1.0f);
    CHECK(first.has_value());
    const std::optional<infinity::platform::InputEvent> restarted = queue.tryPop();
    CHECK(restarted.has_value());
    if (!restarted.has_value()) {
        return;
    }
    CHECK(restarted->sequence == 1);
}

TEST_CASE("InputQueue wraps around the ring without losing order") {
    infinity::platform::InputQueue queue;
    using infinity::platform::InputSource;

    for (std::size_t i = 0; i < infinity::platform::InputQueue::capacity(); ++i) {
        const auto pushed = queue.push(InputSource::KEY, static_cast<std::uint16_t>(i), 1.0f);
        CHECK(pushed.has_value());
    }
    for (std::size_t i = 0; i < infinity::platform::InputQueue::capacity(); ++i) {
        const std::optional<infinity::platform::InputEvent> drained = queue.tryPop();
        CHECK(drained.has_value());
    }
    for (std::size_t i = 0; i < infinity::platform::InputQueue::capacity(); ++i) {
        const auto pushed = queue.push(InputSource::KEY, static_cast<std::uint16_t>(i), 1.0f);
        CHECK(pushed.has_value());
    }

    for (std::size_t i = 0; i < infinity::platform::InputQueue::capacity(); ++i) {
        const std::optional<infinity::platform::InputEvent> event = queue.tryPop();
        CHECK(event.has_value());
        if (!event.has_value()) {
            return;
        }
        CHECK(event->sequence == infinity::platform::InputQueue::capacity() + i + 1);
        CHECK(event->code == i);
    }
}

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
