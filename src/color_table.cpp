#include "color_table.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <zlib.h>

namespace dell1320c {
namespace {

constexpr std::size_t ENTRIES_PER_ROW = 256;
constexpr std::size_t TABLE_ENTRIES = ENTRIES_PER_ROW * ENTRIES_PER_ROW * ENTRIES_PER_ROW;

std::size_t table_index(const std::uint8_t* rgb) noexcept {
    return std::size_t{rgb[0]} << 16 | std::size_t{rgb[1]} << 8 | std::size_t{rgb[2]};
}

template <std::size_t Channels>
void map_pixels(const std::uint8_t* entries, std::span<const std::uint8_t> rgb, std::uint8_t* output) noexcept {
    const std::uint8_t* pixel = rgb.data();
    const std::uint8_t* end = pixel + rgb.size();

    for (; pixel < end; pixel += RGB_CHANNELS) {
        std::copy_n(entries + table_index(pixel) * Channels, Channels, output);
        output += Channels;
    }
}

void undo_delta_coding(std::uint8_t* entries, std::size_t channels) noexcept {
    const std::size_t row_bytes = ENTRIES_PER_ROW * channels;
    const std::size_t row_count = TABLE_ENTRIES / ENTRIES_PER_ROW;

    for (std::size_t row = 0; row < row_count; row++) {
        std::uint8_t* row_start = entries + row * row_bytes;
        for (std::size_t i = channels; i < row_bytes; i++) row_start[i] = static_cast<std::uint8_t>(row_start[i] + row_start[i - channels]);
    }
}

std::expected<std::vector<char>, std::string> read_file(const std::filesystem::path& path) {
    std::error_code      size_error;
    const std::uintmax_t size = std::filesystem::file_size(path, size_error);
    if (size_error || size == 0) return std::unexpected(std::format("unable to read color table {}", path.string()));

    std::vector<char> contents(size);
    std::ifstream     file(path, std::ios::binary);
    if (!file.read(contents.data(), static_cast<std::streamsize>(contents.size())))
        return std::unexpected(std::format("unable to read color table {}", path.string()));
    return contents;
}

}

std::optional<ColorTableChoice> color_table_for(const Settings& settings) {
    if (settings.color_mode == ColorMode::GRAY) return ColorTableChoice{settings.correction_off ? "BlackOff" : "Black", ColorMode::GRAY};
    if (settings.correction_off) return std::nullopt;
    return ColorTableChoice{settings.document_type, ColorMode::COLOR};
}

ColorTable::ColorTable(ColorMode output_mode, std::unique_ptr<std::uint8_t[]> decoded_entries) noexcept
    : output(output_mode), entries(std::move(decoded_entries)) {}

std::expected<ColorTable, std::string> ColorTable::load(const std::filesystem::path& directory, std::string_view name, ColorMode output) {
    const std::filesystem::path path = directory / std::format("{}.lut", name);
    auto                        packed = read_file(path);
    if (!packed) return std::unexpected(packed.error());

    const std::size_t channels = channel_count(output);
    const std::size_t expected_size = TABLE_ENTRIES * channels;
    auto              entries = std::make_unique_for_overwrite<std::uint8_t[]>(expected_size);
    uLongf            unpacked_size = expected_size;
    const auto*       packed_bytes = reinterpret_cast<const Bytef*>(packed->data());
    const int         status = uncompress(entries.get(), &unpacked_size, packed_bytes, packed->size());
    if (status == Z_MEM_ERROR) throw std::bad_alloc();
    if (status != Z_OK || unpacked_size != expected_size) return std::unexpected(std::format("color table {} is corrupt", path.string()));

    undo_delta_coding(entries.get(), channels);
    return ColorTable(output, std::move(entries));
}

std::span<const std::uint8_t> ColorTable::map_line(std::span<const std::uint8_t> rgb, std::span<std::uint8_t> mapped) const {
    if (rgb.size() % RGB_CHANNELS != 0) throw std::invalid_argument("the RGB line is not a whole number of pixels");
    const std::size_t mapped_size = rgb.size() / RGB_CHANNELS * channel_count(output);
    if (mapped.size() < mapped_size) throw std::length_error("the color table output line is too short");

    if (output == ColorMode::GRAY)
        map_pixels<1>(entries.get(), rgb, mapped.data());
    else
        map_pixels<RGB_CHANNELS>(entries.get(), rgb, mapped.data());
    return mapped.first(mapped_size);
}

}
