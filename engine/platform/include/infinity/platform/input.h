// infinity/platform/input.h
//
// Deterministic input (F3.2, ADR-033, rule 11). Input is data: raw events
// (source, code, value) enter the InputQueue and are resolved to action ids
// through the ActionMap. The queue is fixed-size, zero-allocating and ordered,
// so recording the stream reproduces the same simulation on replay (ADR-013):
// sequence numbers are assigned by the queue, never by the backend.
//
//   Overflow policy - a full queue drops the event but the push still reports
//                     success; the drop is visible through wasOverflowed() so
//                     the runtime decides how to react (drain, coalesce, log)
//                     without the backend having to care.
#pragma once

#include "infinity/core/error.h"
#include "infinity/platform/error.h"
#include "infinity/platform/input_source.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace infinity::platform {

inline constexpr std::size_t MAX_QUEUED_EVENTS = 256;
inline constexpr std::size_t MAX_ACTIONS = 256;

using ActionId = std::uint16_t;

// A single raw input sample. value is 0.0f..1.0f for buttons (0/1) and
// -1.0f..1.0f for axes.
struct InputEvent {
    InputSource source;
    std::uint16_t code;
    float value;
    std::uint64_t sequence;
};

// Fixed-size ring of raw input events. Never allocates after construction and
// is cheap enough for the per-frame hot path (rule 03/08).
class InputQueue {
public:
    InputQueue() noexcept = default;

    // Appends one event: validates source/code (INVALID_SOURCE/INVALID_CODE),
    // assigns the next sequence number and stores the event. When the queue
    // is full the event is dropped and the overflow flag is set, but the push
    // still succeeds (documented policy above): a dropped event is data loss
    // the runtime can observe, never a hard failure.
    [[nodiscard]] ExpectedVoid push(InputSource source, std::uint16_t code, float value) noexcept;

    // Removes the oldest event, or nullopt when the queue is empty.
    [[nodiscard]] std::optional<InputEvent> tryPop() noexcept;

    // Drops all events and restarts the sequence at 1 (ADR-013: replay starts
    // from a known point). Also clears the overflow flag.
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return MAX_QUEUED_EVENTS; }
    [[nodiscard]] bool wasOverflowed() const noexcept;

private:
    std::array<InputEvent, MAX_QUEUED_EVENTS> m_events{};
    std::size_t m_head = 0;  ///< next write slot
    std::size_t m_tail = 0;  ///< next read slot
    std::size_t m_count = 0; ///< events currently queued
    std::uint64_t m_nextSequence = 1;
    bool m_overflowed = false;
};

// Maps (source, code) events to action ids. Slots are indexed by ActionId, so
// resolution scans in action id order and returns the lowest bound match.
class ActionMap {
public:
    ActionMap() noexcept = default;

    // Binds (source, code) to actionId, replacing any previous binding on the
    // same action. Out-of-range action id -> ACTION_OUT_OF_RANGE; invalid
    // source/code -> INVALID_SOURCE / INVALID_CODE.
    [[nodiscard]] ExpectedVoid bind(ActionId actionId, InputSource source,
                                    std::uint16_t code) noexcept;

    // Removes the binding on actionId. Unbinding an already-free slot is a
    // no-op that still reports success; out-of-range ids are rejected.
    [[nodiscard]] ExpectedVoid unbind(ActionId actionId) noexcept;

    // Returns the first bound action (lowest ActionId) matching the event, or
    // nullopt when no action is bound to it.
    [[nodiscard]] std::optional<ActionId> resolve(InputSource source,
                                                  std::uint16_t code) const noexcept;

    [[nodiscard]] std::size_t boundCount() const noexcept;
    [[nodiscard]] static constexpr std::size_t maxActions() noexcept { return MAX_ACTIONS; }

private:
    struct Binding {
        InputSource source{InputSource::KEY};
        std::uint16_t code{0};
        bool bound{false};
    };

    std::array<Binding, MAX_ACTIONS> m_slots{};
    std::size_t m_bound = 0;
};

} // namespace infinity::platform
