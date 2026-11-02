#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "sq21.hpp"

using dell1320c::ColorMode;
using dell1320c::Resolution;

namespace {

constexpr std::string_view PROGRAM_NAME = "sq21-encode";

std::optional<ColorMode> parse_color_mode(std::string_view text) {
    if (text == "rgb") return ColorMode::COLOR;
    if (text == "gray") return ColorMode::GRAY;
    return std::nullopt;
}

std::optional<std::uint32_t> parse_width(std::string_view text) {
    std::uint32_t width = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), width);
    if (error != std::errc() || end != text.data() + text.size() || width == 0) return std::nullopt;
    return width;
}

std::optional<Resolution> parse_resolution(std::string_view text) {
    if (text == "300") return Resolution::DPI_300;
    if (text == "600") return Resolution::DPI_600;
    return std::nullopt;
}

std::vector<std::uint8_t> read_standard_input() {
    std::vector<std::uint8_t>         contents;
    std::array<std::uint8_t, 1 << 16> chunk{};

    while (const std::size_t count = std::fread(chunk.data(), 1, chunk.size(), stdin))
        contents.insert(contents.end(), chunk.data(), chunk.data() + count);
    if (std::ferror(stdin)) throw std::runtime_error("unable to read standard input");
    return contents;
}

int run(std::span<char*> arguments) {
    std::optional<ColorMode>     color_mode;
    std::optional<std::uint32_t> width;
    std::optional<Resolution>    resolution;
    if (arguments.size() == 4) {
        color_mode = parse_color_mode(arguments[1]);
        width = parse_width(arguments[2]);
        resolution = parse_resolution(arguments[3]);
    }
    if (!color_mode || !width || !resolution) {
        std::string_view program = arguments.empty() ? PROGRAM_NAME : std::string_view(arguments[0]);
        std::println(stderr, "Usage: {} rgb|gray <width> 300|600 < pixels > compressed", program);
        return 2;
    }

    std::vector<std::uint8_t> pixels = read_standard_input();
    std::vector<std::uint8_t> compressed = dell1320c::sq21::compress(*color_mode, pixels, *width, *resolution);
    const bool                written = std::fwrite(compressed.data(), 1, compressed.size(), stdout) == compressed.size();
    if (!written || std::fflush(stdout) != 0) throw std::runtime_error("unable to write the compressed data");
    return 0;
}

}

int main(int argc, char* argv[]) {
    try {
        return run(std::span(argv, static_cast<std::size_t>(argc)));
    } catch (const std::exception& error) {
        std::fputs("sq21-encode: ", stderr);
        std::fputs(error.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
