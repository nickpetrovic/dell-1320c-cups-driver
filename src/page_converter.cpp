#include "page_converter.hpp"

#include <algorithm>
#include <cstdio>
#include <format>
#include <print>
#include <span>
#include <utility>

#include "sq21.hpp"
#include "unique_c_pointer.hpp"

namespace dell1320c {
namespace {

constexpr std::uint8_t WHITE = 0xff;

using File = UniqueCPointer<std::FILE, [](std::FILE* file) { std::fclose(file); }>;

bool is_white(std::span<const std::uint8_t> line) {
    return std::ranges::all_of(line, [](std::uint8_t value) { return value == WHITE; });
}

void reverse_pixels(std::span<const std::uint8_t> source, std::span<std::uint8_t> target) {
    const std::uint8_t* pixel = source.data() + source.size();
    std::uint8_t*       output = target.data();

    while (pixel > source.data()) {
        pixel -= RGB_CHANNELS;
        output = std::copy_n(pixel, RGB_CHANNELS, output);
    }
}

File open_dump(const std::filesystem::path& directory, unsigned page_number, const Crop& crop, std::size_t channels) {
    if (directory.empty()) return nullptr;

    const std::filesystem::path path = directory / std::format("page-{}-{}x{}x{}.raw", page_number, crop.width, crop.height, channels);
    File                        file(std::fopen(path.c_str(), "wb"));
    if (!file) std::println(stderr, "DEBUG: unable to write {}", path.string());
    return file;
}

}

std::uint32_t printable_extent(float low, float high, Resolution resolution) {
    if (high <= low) return 0;
    const float         extent_pixels = (high - low) * static_cast<float>(dots_per_inch(resolution));
    const double        exact = static_cast<double>(extent_pixels) / static_cast<double>(POINTS_PER_INCH);
    const auto          whole = static_cast<std::uint32_t>(exact);
    const double        fraction = exact - static_cast<double>(whole);
    const std::uint32_t rounding = fraction >= 0.5 ? 1 : 0;
    return whole + rounding;
}

Crop centered_crop(PixelSize raster, PixelSize printable) {
    Crop crop{.width = raster.width, .height = raster.height};

    if (printable.width < raster.width) {
        crop.left = (raster.width - printable.width) / 2;
        crop.width = printable.width;
    }
    if (printable.height < raster.height) {
        crop.top = (raster.height - printable.height) / 2;
        crop.height = printable.height;
    }
    return crop;
}

PageConverter::PageConverter(const Settings& print_settings, const ColorTable* table, std::filesystem::path dump_path)
    : settings(print_settings), color_table(table), dump_directory(std::move(dump_path)) {}

bool PageConverter::should_rotate(unsigned page_number) const {
    const bool back_of_long_edge_sheet = settings.duplex == DuplexMode::LONG_EDGE && page_number % 2 == 0;
    return settings.rotate != back_of_long_edge_sheet;
}

PageResult PageConverter::convert(RasterReader& reader, const cups_page_header2_t& header, unsigned page_number) {
    if (!is_supported_raster(header)) {
        return std::unexpected(
            std::format(
                "unsupported raster page: {} pixels wide, color space {}, {} bits per color, {} bits per pixel, color order {}, {} bytes per line",
                header.cupsWidth, static_cast<unsigned>(header.cupsColorSpace), header.cupsBitsPerColor, header.cupsBitsPerPixel,
                static_cast<unsigned>(header.cupsColorOrder), header.cupsBytesPerLine
            )
        );
    }

    const auto job_dpi = static_cast<unsigned>(dots_per_inch(settings.resolution));
    const bool resolution_matches = header.HWResolution[0] == job_dpi && header.HWResolution[1] == job_dpi;
    if (!resolution_matches) {
        return std::unexpected(
            std::format("the raster page is {}x{} dpi, but the job is {} dpi", header.HWResolution[0], header.HWResolution[1], job_dpi)
        );
    }

    const Paper&        paper = settings.paper;
    const PixelSize     raster_size{.width = header.cupsWidth, .height = header.cupsHeight};
    const std::uint32_t printable_width = printable_extent(paper.left, paper.right, settings.resolution);
    const std::uint32_t printable_height = printable_extent(paper.bottom, paper.top, settings.resolution);
    const Crop          crop = centered_crop(raster_size, {.width = printable_width, .height = printable_height});
    const std::size_t   crop_offset = std::size_t{crop.left} * RGB_CHANNELS;
    const std::size_t   crop_size = std::size_t{crop.width} * RGB_CHANNELS;
    const ColorMode     color_mode = color_table ? color_table->output_mode() : ColorMode::COLOR;
    const std::size_t   channels = channel_count(color_mode);
    const bool          rotate = should_rotate(page_number);
    bool                blank = settings.skip_blank_pages;
    sq21::Compressor    compressor(color_mode, crop.width, settings.resolution);
    File                dump = open_dump(dump_directory, page_number, crop, channels);

    raster_line.resize(header.cupsBytesPerLine);
    mapped_line.resize(std::size_t{crop.width} * channels);
    if (rotate) {
        rotation_buffer.resize(crop_size * crop.height);
        rotated_line.resize(crop_size);
    }

    const auto add_line = [&](std::span<const std::uint8_t> rgb) {
        if (blank) blank = is_white(rgb);
        const std::span<const std::uint8_t> line = color_table ? color_table->map_line(rgb, mapped_line) : rgb;
        if (dump) std::fwrite(line.data(), 1, line.size(), dump.get());
        compressor.add_line(line);
    };

    const std::uint32_t crop_end = crop.top + crop.height;
    for (std::uint32_t row = 0; row < header.cupsHeight; row++) {
        if (!reader.read_line(raster_line)) return std::unexpected(std::format("the raster data ended early on line {}", row));
        if (row < crop.top || row >= crop_end) continue;

        const std::span<const std::uint8_t> cropped = std::span(raster_line).subspan(crop_offset, crop_size);
        if (rotate)
            std::ranges::copy(cropped, rotation_buffer.data() + std::size_t{row - crop.top} * crop_size);
        else
            add_line(cropped);
    }
    if (rotate) {
        for (std::uint32_t row = crop.height; row-- > 0;) {
            reverse_pixels(std::span(rotation_buffer).subspan(std::size_t{row} * crop_size, crop_size), rotated_line);
            add_line(rotated_line);
        }
    }

    if (blank) return std::optional<hbpl::Page>();
    return hbpl::Page{
        .width = sq21::padded_width(crop.width),
        .height = crop.height,
        .resolution = settings.resolution,
        .color_mode = color_mode,
        .compressed = std::move(compressor).finish(),
    };
}

}
