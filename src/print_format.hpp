#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

namespace dell1320c {

constexpr float       POINTS_PER_INCH = 72.0f;
constexpr std::size_t RGB_CHANNELS = 3;

enum class ColorMode : std::uint8_t {
    COLOR,
    GRAY,
};

enum class Resolution : std::uint16_t {
    DPI_300 = 300,
    DPI_600 = 600,
};

constexpr std::size_t channel_count(ColorMode mode) noexcept {
    return mode == ColorMode::GRAY ? 1 : RGB_CHANNELS;
}

constexpr int dots_per_inch(Resolution resolution) noexcept {
    return std::to_underlying(resolution);
}

}
