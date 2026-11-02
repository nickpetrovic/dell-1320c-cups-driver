#include <array>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "color_table.hpp"
#include "testing.hpp"

namespace dell1320c::testing {
namespace {

std::filesystem::path color_directory() {
    const char* path = std::getenv("DELL1320C_COLOR_DIR");
    if (!path || !*path) throw std::runtime_error("set DELL1320C_COLOR_DIR to the color table directory");
    return path;
}

ColorTable load(std::string_view name, ColorMode output) {
    auto table = ColorTable::load(color_directory(), name, output);
    if (!table) throw std::runtime_error(table.error());
    return std::move(*table);
}

void color_entries_match_the_recorded_tables() {
    constexpr std::array<std::uint8_t, 12> LINE = {0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x12, 0x80, 0xfe, 0xfe, 0x80, 0x12};
    std::array<std::uint8_t, 12>           mapped{};

    check_equal(hex(load("Normal", ColorMode::COLOR).map_line(LINE, mapped)), std::string("ffffff0000002e98f7fe4800"));
    check_equal(hex(load("Photo", ColorMode::COLOR).map_line(LINE, mapped)), std::string("ffffff0000012a98ffff5500"));
}

void gray_entries_match_the_recorded_tables() {
    constexpr std::array<std::uint8_t, 12> LINE = {0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x12, 0x80, 0xfe, 0xfe, 0x80, 0x12};
    std::array<std::uint8_t, 4>            mapped{};
    const ColorTable                       black = load("Black", ColorMode::GRAY);

    check_equal(black.output_mode(), ColorMode::GRAY);
    check_equal(hex(black.map_line(LINE, mapped)), std::string("ff006a99"));
    check_equal(hex(load("BlackOff", ColorMode::GRAY).map_line(LINE, mapped)), std::string("ff006c99"));
}

void bad_lines_are_rejected() {
    ColorTable                  table = load("Black", ColorMode::GRAY);
    std::array<std::uint8_t, 6> line{};
    std::array<std::uint8_t, 1> short_output{};
    std::array<std::uint8_t, 4> partial_pixel{};
    std::array<std::uint8_t, 2> mapped{};

    check_throws<std::length_error>([&] { static_cast<void>(table.map_line(line, short_output)); });
    check_throws<std::invalid_argument>([&] { static_cast<void>(table.map_line(partial_pixel, mapped)); });
}

void missing_and_corrupt_tables_are_errors() {
    const TemporaryDirectory directory;
    auto                     missing = ColorTable::load(directory.path(), "Normal", ColorMode::COLOR);
    check(!missing.has_value(), "a missing table fails to load");
    if (!missing) check(missing.error().contains("Normal.lut"), "the error names the file");

    std::ofstream(directory.path() / "Normal.lut", std::ios::binary) << "not a color table";
    auto corrupt = ColorTable::load(directory.path(), "Normal", ColorMode::COLOR);
    check(!corrupt.has_value(), "a corrupt table fails to load");
    if (!corrupt) check(corrupt.error().contains("corrupt"), "the error says the table is corrupt");
}

constexpr std::array TESTS = {
    TestCase{"color table: color entries match the recorded tables", color_entries_match_the_recorded_tables},
    TestCase{"color table: gray entries match the recorded tables",  gray_entries_match_the_recorded_tables },
    TestCase{"color table: bad lines are rejected",                  bad_lines_are_rejected                 },
    TestCase{"color table: missing and corrupt tables fail",         missing_and_corrupt_tables_are_errors  },
};

}

std::span<const TestCase> color_table_tests() {
    return TESTS;
}

}
