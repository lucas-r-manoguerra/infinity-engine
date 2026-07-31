// infinity/core/version.h
#pragma once

#include <string_view>

namespace infinity::core {

// Returns the semantic version of the engine (ADR-049).
[[nodiscard]] std::string_view version() noexcept;

} // namespace infinity::core
