#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "sq21.hpp"
#include "testing.hpp"

namespace dell1320c::testing {
namespace {

struct KnownOutput {
    std::string_view          description;
    ColorMode                 color_mode;
    std::uint32_t             width;
    Resolution                resolution;
    std::vector<std::uint8_t> pixels;
    std::string_view          expected;
};

std::vector<std::uint8_t> filled(std::size_t size, std::uint8_t value) {
    return std::vector<std::uint8_t>(size, value);
}

std::vector<std::uint8_t> noise_line_then_white_line() {
    std::vector<std::uint8_t> pixels;
    for (std::size_t i = 0; i < 64; i++) pixels.push_back(static_cast<std::uint8_t>((i * 73 + 41) % 251));
    pixels.insert(pixels.end(), 64, 0xff);
    return pixels;
}

std::vector<std::uint8_t> slightly_varying_color() {
    std::vector<std::uint8_t> pixels;
    for (int i = 0; i < 16 * 3 * 3; i++) pixels.push_back(static_cast<std::uint8_t>(std::clamp(124 + (i * 13) % 9 + i / 48 * 3, 0, 255)));
    return pixels;
}

constexpr std::string_view CODE_TABLE_SWITCH_OUTPUT(
    "b337337338bc35cadccdcf5ccdcc8bca5c85ccdcf5ccdccdcf5ccdccdccdcf5ccdccdcf5ccdcc8b9fb97b99b9eb99b99178cb93b99b9eb99b99b9eb99b99b99b9eb99b99b9eb99b99b99b9eb99b99b9eb99b991785b95b99b9eb99f8b77bff"
);
constexpr std::string_view COLOR_AVERAGING_OUTPUT = "87adfdffad8e11ba60c10aad94b870e1add0b87005ff";

void known_outputs_match_the_vendor_encoder() {
    std::vector<std::uint8_t> rgb_ramp = {
        0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0,
        0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe1,
    };
    std::vector<std::uint8_t> gray_ramp = {
        0x00, 0x20, 0x40, 0x60, 0x80, 0xa0, 0x01, 0x21, 0x41, 0x61, 0x81, 0xa1, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    };
    std::vector<std::uint8_t>      noisy_lines = noise_line_then_white_line();
    std::vector<std::uint8_t>      varying_color = slightly_varying_color();
    const std::vector<KnownOutput> known_outputs = {
        {"white RGB line", ColorMode::COLOR, 8, Resolution::DPI_600, filled(24, 0xff), "5bff"},
        {"white gray line", ColorMode::GRAY, 8, Resolution::DPI_600, filled(8, 0xff), "92bfff"},
        {"RGB ramp", ColorMode::COLOR, 5, Resolution::DPI_600, rgb_ramp, "81c0e461c0701c04380e03808701c07010e0380e0361ff"},
        {"gray ramp at 300 dpi", ColorMode::GRAY, 6, Resolution::DPI_300, gray_ramp, "1642c8590b21642f1e4b543417ff"},
        {"run longer than a chunk", ColorMode::GRAY, 20000, Resolution::DPI_600, filled(20000, 0xff), "93ffffdf91b9ff"},
        {"black gray lines", ColorMode::GRAY, 9, Resolution::DPI_600, filled(18, 0x00), "5e4a337fff"},
        {"gray line ending in the marker value", ColorMode::GRAY, 8, Resolution::DPI_600, filled(8, 0x1f), "b1eaffff"},
        {"code table switch after a noisy line", ColorMode::GRAY, 64, Resolution::DPI_600, noisy_lines, CODE_TABLE_SWITCH_OUTPUT},
        {"color error averaging", ColorMode::COLOR, 16, Resolution::DPI_600, varying_color, COLOR_AVERAGING_OUTPUT},
    };

    for (const KnownOutput& known : known_outputs) {
        std::string actual = hex(sq21::compress(known.color_mode, known.pixels, known.width, known.resolution));
        if (actual != known.expected) report_failure(std::source_location::current(), std::format("{}: got {}", known.description, actual));
    }
}

void bad_input_is_rejected() {
    std::vector<std::uint8_t> short_line(5);
    sq21::Compressor          compressor(ColorMode::GRAY, 8, Resolution::DPI_600);

    check_throws<std::invalid_argument>([&] { compressor.add_line(short_line); });
    check_throws<std::invalid_argument>([&] { static_cast<void>(sq21::compress(ColorMode::COLOR, short_line, 2, Resolution::DPI_600)); });
    check_throws<std::length_error>([] {
        const sq21::Compressor too_wide(ColorMode::GRAY, sq21::MAX_LINE_WIDTH + 1, Resolution::DPI_600);
    });
}

constexpr std::array TESTS = {
    TestCase{"sq21: known outputs match the vendor encoder", known_outputs_match_the_vendor_encoder},
    TestCase{"sq21: bad input is rejected",                  bad_input_is_rejected                 },
};

}

std::span<const TestCase> sq21_tests() {
    return TESTS;
}

}
