#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "color_table.hpp"
#include "hbpl.hpp"
#include "print_format.hpp"
#include "raster_reader.hpp"
#include "settings.hpp"

namespace dell1320c {

struct PixelSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct Crop {
    std::uint32_t left = 0;
    std::uint32_t top = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

[[nodiscard]] std::uint32_t printable_extent(float low, float high, Resolution resolution);
[[nodiscard]] Crop centered_crop(PixelSize raster, PixelSize printable);

using PageResult = std::expected<std::optional<hbpl::Page>, std::string>;

class PageConverter {
public:
    PageConverter(const Settings& print_settings, const ColorTable* table, std::filesystem::path dump_path = {});
    PageConverter(Settings&&, const ColorTable*, std::filesystem::path = {}) = delete;
    PageConverter(const PageConverter&) = delete;
    PageConverter& operator=(const PageConverter&) = delete;
    PageConverter(PageConverter&&) = delete;
    PageConverter& operator=(PageConverter&&) = delete;
    ~PageConverter() = default;

    [[nodiscard]] PageResult convert(RasterReader& reader, const cups_page_header2_t& header, unsigned page_number);

private:
    [[nodiscard]] bool should_rotate(unsigned page_number) const;

    const Settings&           settings;
    const ColorTable*         color_table;
    std::filesystem::path     dump_directory;
    std::vector<std::uint8_t> raster_line;
    std::vector<std::uint8_t> rotated_line;
    std::vector<std::uint8_t> rotation_buffer;
    std::vector<std::uint8_t> mapped_line;
};

}
