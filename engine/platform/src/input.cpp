// infinity/platform/src/input.cpp
#include "infinity/platform/input.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace infinity::platform {

namespace {

// Shared validation for raw (source, code) events: an unknown source is an
// INVALID_SOURCE, a code outside its source's range is an INVALID_CODE. The
// queue and the action map accept exactly the same set of events.
[[nodiscard]] std::optional<PlatformError> validationError(InputSource source,
                                                           std::uint16_t code) noexcept {
    switch (source) {
    case InputSource::KEY:
        if (code >= KEY_COUNT) {
            return PlatformError::INVALID_CODE;
        }
        return std::nullopt;
    case InputSource::MOUSE_BUTTON:
        if (code >= MOUSE_BUTTON_COUNT) {
            return PlatformError::INVALID_CODE;
        }
        return std::nullopt;
    case InputSource::GAMEPAD_BUTTON:
        if (code >= GAMEPAD_BUTTON_COUNT) {
            return PlatformError::INVALID_CODE;
        }
        return std::nullopt;
    case InputSource::AXIS:
        if (code >= AXIS_COUNT) {
            return PlatformError::INVALID_CODE;
        }
        return std::nullopt;
    }
    return PlatformError::INVALID_SOURCE;
}

} // namespace

ExpectedVoid InputQueue::push(InputSource source, std::uint16_t code, float value) noexcept {
    if (const std::optional<PlatformError> error = validationError(source, code)) {
        return std::unexpected(*error);
    }
    if (m_count == m_events.size()) {
        // Full: drop the event but report success (documented policy in
        // input.h). The drop is visible through wasOverflowed(), so the
        // runtime decides how to react without failing the backend call.
        m_overflowed = true;
        return {};
    }
    m_events[m_head] =
        InputEvent{.source = source, .code = code, .value = value, .sequence = m_nextSequence++};
    m_head = (m_head + 1) % m_events.size();
    ++m_count;
    return {};
}

std::optional<InputEvent> InputQueue::tryPop() noexcept {
    if (m_count == 0) {
        return std::nullopt;
    }
    const InputEvent& event = m_events[m_tail];
    m_tail = (m_tail + 1) % m_events.size();
    --m_count;
    return event;
}

void InputQueue::clear() noexcept {
    m_head = 0;
    m_tail = 0;
    m_count = 0;
    m_nextSequence = 1;
    m_overflowed = false;
}

std::size_t InputQueue::size() const noexcept { return m_count; }

bool InputQueue::wasOverflowed() const noexcept { return m_overflowed; }

ExpectedVoid ActionMap::bind(ActionId actionId, InputSource source, std::uint16_t code) noexcept {
    if (actionId >= MAX_ACTIONS) {
        return std::unexpected(PlatformError::ACTION_OUT_OF_RANGE);
    }
    if (const std::optional<PlatformError> error = validationError(source, code)) {
        return std::unexpected(*error);
    }
    Binding& slot = m_slots[actionId];
    if (!slot.bound) {
        ++m_bound;
    }
    slot = Binding{.source = source, .code = code, .bound = true};
    return {};
}

ExpectedVoid ActionMap::unbind(ActionId actionId) noexcept {
    if (actionId >= MAX_ACTIONS) {
        return std::unexpected(PlatformError::ACTION_OUT_OF_RANGE);
    }
    Binding& slot = m_slots[actionId];
    if (slot.bound) {
        slot = Binding{};
        --m_bound;
    }
    return {};
}

std::optional<ActionId> ActionMap::resolve(InputSource source, std::uint16_t code) const noexcept {
    for (std::size_t actionId = 0; actionId < MAX_ACTIONS; ++actionId) {
        const Binding& slot = m_slots[actionId];
        if (slot.bound && slot.source == source && slot.code == code) {
            return static_cast<ActionId>(actionId);
        }
    }
    return std::nullopt;
}

std::size_t ActionMap::boundCount() const noexcept { return m_bound; }

} // namespace infinity::platform
