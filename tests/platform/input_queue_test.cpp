// tests/platform/input_queue_test.cpp
//
// Contract tests for the InputQueue and the input code vocabulary (F3.2,
// ADR-033): codeValid accepts/rejects per-source codes (the source vocabulary
// belongs to the input domain), and the InputQueue preserves order and assigns
// sequence numbers, rejects malformed events, drops instead of failing on
// overflow (visible via wasOverflowed) and resets cleanly. The ActionMap cases
// live in input_action_map_test.cpp (rule 01: One File = One Task).
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
