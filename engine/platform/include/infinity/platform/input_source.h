// infinity/platform/input_source.h
//
// Input source vocabulary (F3.2, ADR-033). The physical sources and their
// code ranges are data-only vocabulary shared by the input queue, the action
// map and the future backends; keeping them in their own header lets the
// input model compile without depending on the window abstraction (rule 01:
// data first, backends later).
#pragma once

#include <cstdint>

namespace infinity::platform {

// Input sources (ADR-033). AXIS covers analog values (gamepad sticks); the
// analog/digital split happens in the runtime through the action map.
enum class InputSource : uint8_t {
    KEY = 0,
    MOUSE_BUTTON,
    GAMEPAD_BUTTON,
    AXIS,
};

inline constexpr std::uint16_t KEY_COUNT = 256;          ///< keyboard scancodes
inline constexpr std::uint8_t MOUSE_BUTTON_COUNT = 8;    ///< mouse buttons
inline constexpr std::uint8_t GAMEPAD_BUTTON_COUNT = 16; ///< gamepad buttons
inline constexpr std::uint8_t AXIS_COUNT = 16;           ///< analog axes

// Returns true when code is inside source's code range. An out-of-range value
// of source itself (corrupted) reports false.
[[nodiscard]] constexpr bool codeValid(InputSource source, std::uint16_t code) noexcept {
    switch (source) {
    case InputSource::KEY:
        return code < KEY_COUNT;
    case InputSource::MOUSE_BUTTON:
        return code < MOUSE_BUTTON_COUNT;
    case InputSource::GAMEPAD_BUTTON:
        return code < GAMEPAD_BUTTON_COUNT;
    case InputSource::AXIS:
        return code < AXIS_COUNT;
    }
    return false;
}

} // namespace infinity::platform
