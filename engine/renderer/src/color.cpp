// infinity/renderer/src/color.cpp
//
// Implementation of the precomputed linear-to-sRGB lookup table (G2). The
// table is built once, on first use, from the exact IEC 61966-2-1 curve
// (color.h) with the same rounding contract as the pixel pack path; after
// init it is const data, so the raster hot path pays a single lookup per
// channel instead of a per-pixel std::pow.
#include "infinity/renderer/color.h"

#include <array>
#include <cstdint>

namespace infinity::renderer {

namespace {

// Rounds an sRGB float channel to its byte value: trunc(0.5 + 255 * channel),
// the same contract the pixel pack path uses.
[[nodiscard]] std::uint8_t srgbByte(float linear) {
    // NOLINTNEXTLINE(bugprone-incorrect-roundings) -- contract: +0.5 then truncate.
    return static_cast<std::uint8_t>(0.5f + (255.0f * linearChannelToSrgb(linear)));
}

} // namespace

const std::array<std::uint8_t, 1024>& srgbLookupTable() noexcept {
    static const std::array<std::uint8_t, 1024> table = [] {
        std::array<std::uint8_t, 1024> entries{};
        for (std::size_t i = 0; i < entries.size(); ++i) {
            entries[i] = srgbByte(static_cast<float>(i) / 1023.0f);
        }
        return entries;
    }();
    return table;
}

} // namespace infinity::renderer
