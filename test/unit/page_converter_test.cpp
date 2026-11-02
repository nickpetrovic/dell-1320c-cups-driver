#include <array>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>

#include "color_table.hpp"
#include "file_descriptor.hpp"
#include "page_converter.hpp"
#include "raster_reader.hpp"
#include "sq21.hpp"
#include "testing.hpp"
#include "unique_c_pointer.hpp"

namespace dell1320c::testing {
namespace {

struct RasterPage {
    std::uint32_t             width;
    std::uint32_t             height;
    std::vector<std::uint8_t> rgb;
    std::uint32_t             declared_height = 0;
    cups_cspace_t             color_space = CUPS_CSPACE_RGB;
    unsigned                  resolution = 600;
};

class RasterFile {
public:
    explicit RasterFile(std::vector<RasterPage> pages, cups_mode_t mode = CUPS_RASTER_WRITE) : file(directory.path() / "input.ras") {
        const FileDescriptor descriptor(::open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600));
        if (descriptor.get() < 0) throw std::runtime_error("unable to create the test raster");

        const UniqueCPointer<cups_raster_t, cupsRasterClose> raster(cupsRasterOpen(descriptor.get(), mode));
        if (!raster) throw std::runtime_error("unable to start the test raster");
        for (RasterPage& page : pages) {
            cups_page_header2_t header{};
            header.cupsWidth = page.width;
            header.cupsHeight = page.declared_height ? page.declared_height : page.height;
            header.cupsBitsPerColor = 8;
            header.cupsBitsPerPixel = 24;
            header.cupsBytesPerLine = page.width * 3;
            header.cupsColorOrder = CUPS_ORDER_CHUNKED;
            header.cupsColorSpace = page.color_space;
            header.cupsNumColors = 3;
            header.HWResolution[0] = page.resolution;
            header.HWResolution[1] = page.resolution;
            const auto size = static_cast<unsigned>(page.rgb.size());
            if (!cupsRasterWriteHeader2(raster.get(), &header)) throw std::runtime_error("unable to write a test raster header");
            if (cupsRasterWritePixels(raster.get(), page.rgb.data(), size) != size) throw std::runtime_error("unable to write test pixels");
        }
    }

    RasterReader open() const {
        auto reader = RasterReader::open(file.c_str());
        if (!reader) throw std::runtime_error(reader.error());
        return std::move(*reader);
    }

    const std::filesystem::path& path() const noexcept {
        return file;
    }

    const std::filesystem::path& dump_directory() const noexcept {
        return directory.path();
    }

    std::vector<std::uint8_t> dump(unsigned page_number, std::uint32_t width, std::uint32_t height, unsigned channels) const {
        const std::filesystem::path path = directory.path() / std::format("page-{}-{}x{}x{}.raw", page_number, width, height, channels);
        const std::string           contents = read_file(path);
        return {contents.begin(), contents.end()};
    }

private:
    TemporaryDirectory    directory;
    std::filesystem::path file;
};

Settings test_settings() {
    Settings settings;
    settings.paper = {.width = 612, .length = 792, .left = 0, .bottom = 0, .right = 612, .top = 792};
    settings.correction_off = true;
    return settings;
}

std::vector<std::uint8_t> numbered_pixels(std::size_t pixel_count) {
    std::vector<std::uint8_t> rgb;
    for (std::size_t pixel = 0; pixel < pixel_count; pixel++) rgb.insert(rgb.end(), 3, static_cast<std::uint8_t>(pixel + 1));
    return rgb;
}

PageResult convert_next(RasterReader& reader, PageConverter& converter, unsigned page_number) {
    PageHeaderResult header = reader.next_page();
    if (!header) throw std::runtime_error(header.error());
    if (!*header) throw std::runtime_error("the test raster has no more pages");
    return converter.convert(reader, **header, page_number);
}

hbpl::Page converted_page(RasterReader& reader, PageConverter& converter, unsigned page_number) {
    auto page = convert_next(reader, converter, page_number);
    if (!page) throw std::runtime_error(page.error());
    if (!*page) throw std::runtime_error("the page was skipped");
    return std::move(**page);
}

void rotation_reverses_lines_and_pixels() {
    const RasterFile raster({
        {.width = 2, .height = 2, .rgb = numbered_pixels(4)}
    });
    Settings         settings = test_settings();
    settings.rotate = true;
    PageConverter    converter(settings, nullptr, raster.dump_directory());
    RasterReader     reader = raster.open();
    const hbpl::Page page = converted_page(reader, converter, 1);

    std::vector<std::uint8_t> rotated = {4, 4, 4, 3, 3, 3, 2, 2, 2, 1, 1, 1};
    check(raster.dump(1, 2, 2, 3) == rotated, "the page is turned 180 degrees");
    check_equal(page.width, 8u);
    check_equal(page.height, 2u);
    check_equal(page.color_mode, ColorMode::COLOR);
    check(page.compressed == sq21::compress(ColorMode::COLOR, rotated, 2, Resolution::DPI_600), "the rotated pixels are compressed");
}

void long_edge_duplex_turns_back_pages() {
    const RasterFile raster({
        {.width = 2, .height = 1, .rgb = numbered_pixels(2)},
        {.width = 2, .height = 1, .rgb = numbered_pixels(2)}
    });
    Settings         settings = test_settings();
    settings.duplex = DuplexMode::LONG_EDGE;
    PageConverter converter(settings, nullptr, raster.dump_directory());
    RasterReader  reader = raster.open();

    converted_page(reader, converter, 1);
    converted_page(reader, converter, 2);
    check(raster.dump(1, 2, 1, 3) == std::vector<std::uint8_t>{1, 1, 1, 2, 2, 2}, "the front page is upright");
    check(raster.dump(2, 2, 1, 3) == std::vector<std::uint8_t>{2, 2, 2, 1, 1, 1}, "the back page is turned");
}

void the_crop_is_centered() {
    const RasterFile raster({
        {.width = 6, .height = 4, .rgb = numbered_pixels(24)}
    });
    Settings         settings = test_settings();
    settings.paper.right = 4 * 72.0f / 600;
    settings.paper.top = 2 * 72.0f / 600;
    PageConverter    converter(settings, nullptr, raster.dump_directory());
    RasterReader     reader = raster.open();
    const hbpl::Page page = converted_page(reader, converter, 1);

    constexpr std::array<std::uint8_t, 8> KEPT_PIXELS = {8, 9, 10, 11, 14, 15, 16, 17};
    std::vector<std::uint8_t>             expected;
    for (std::uint8_t pixel : KEPT_PIXELS) expected.insert(expected.end(), 3, pixel);
    check(raster.dump(1, 4, 2, 3) == expected, "the middle of the raster is kept");
    check_equal(page.height, 2u);
}

void an_odd_crop_keeps_the_extra_pixel_on_the_right() {
    const RasterFile raster({
        {.width = 7, .height = 1, .rgb = numbered_pixels(7)}
    });
    Settings         settings = test_settings();
    settings.paper.right = 4 * 72.0f / 600;
    PageConverter converter(settings, nullptr, raster.dump_directory());
    RasterReader  reader = raster.open();
    static_cast<void>(converted_page(reader, converter, 1));

    std::vector<std::uint8_t> expected;
    for (std::uint8_t pixel = 2; pixel <= 5; pixel++) expected.insert(expected.end(), 3, pixel);
    check(raster.dump(1, 4, 1, 3) == expected, "the crop starts one pixel in, rounding down");
}

void blank_pages_are_skipped_when_asked() {
    const std::vector<std::uint8_t> white(std::size_t{4} * 2 * 3, 0xff);
    std::vector<std::uint8_t> almost_white = white;
    almost_white[5] = 0xfe;
    const RasterFile raster({
        {.width = 4, .height = 2, .rgb = white       },
        {.width = 4, .height = 2, .rgb = almost_white}
    });

    Settings settings = test_settings();
    settings.skip_blank_pages = true;
    PageConverter converter(settings, nullptr);
    RasterReader  reader = raster.open();
    auto          blank = convert_next(reader, converter, 1);
    auto          marked = convert_next(reader, converter, 1);

    check(blank.has_value() && !blank->has_value(), "the white page is skipped");
    check(marked.has_value() && marked->has_value(), "a page with one mark is kept");

    const Settings default_settings = test_settings();
    RasterReader   second_reader = raster.open();
    PageConverter  keeping_converter(default_settings, nullptr);
    auto           kept = convert_next(second_reader, keeping_converter, 1);
    check(kept.has_value() && kept->has_value(), "white pages are kept by default");
}

void the_black_table_makes_gray_pages() {
    const char* color_directory = std::getenv("DELL1320C_COLOR_DIR");
    if (!color_directory) throw std::runtime_error("set DELL1320C_COLOR_DIR to the color table directory");
    auto table = ColorTable::load(color_directory, "Black", ColorMode::GRAY);
    if (!table) throw std::runtime_error(table.error());

    const RasterFile raster({
        {.width = 3, .height = 2, .rgb = numbered_pixels(6)}
    });
    Settings         settings = test_settings();
    settings.color_mode = ColorMode::GRAY;
    PageConverter    converter(settings, &*table, raster.dump_directory());
    RasterReader     reader = raster.open();
    const hbpl::Page page = converted_page(reader, converter, 1);

    std::vector<std::uint8_t>           rgb = numbered_pixels(6);
    std::vector<std::uint8_t>           expected(6);
    const std::span<const std::uint8_t> mapped = table->map_line(rgb, expected);
    check_equal(page.color_mode, ColorMode::GRAY);
    check(raster.dump(1, 3, 2, 1) == std::vector<std::uint8_t>(mapped.begin(), mapped.end()), "the page holds the table's gray values");
}

enum class RasterFormat {
    VERSION_1,
    VERSION_3,
    COMPRESSED,
    APPLE,
};

void convert_to_version_1(const std::filesystem::path& path, std::size_t page_count, std::size_t pixel_bytes) {
    const std::string version_3 = read_file(path);

    constexpr std::uint32_t SYNC = CUPS_RASTER_SYNCv1;
    std::string             version_1(sizeof(SYNC), '\0');
    std::memcpy(version_1.data(), &SYNC, sizeof(SYNC));
    std::size_t position = sizeof(SYNC);
    for (std::size_t page = 0; page < page_count; page++) {
        version_1.append(version_3, position, sizeof(cups_page_header_t));
        position += sizeof(cups_page_header2_t);
        version_1.append(version_3, position, pixel_bytes);
        position += pixel_bytes;
    }
    std::ofstream(path, std::ios::binary | std::ios::trunc) << version_1;
}

cups_mode_t write_mode(RasterFormat format) {
    if (format == RasterFormat::COMPRESSED) return CUPS_RASTER_WRITE_COMPRESSED;
    if (format == RasterFormat::APPLE) return CUPS_RASTER_WRITE_APPLE;
    return CUPS_RASTER_WRITE;
}

std::vector<RasterPage> pages_for(RasterFormat format, std::size_t page_count) {
    const cups_cspace_t color_space = format == RasterFormat::APPLE ? CUPS_CSPACE_SRGB : CUPS_CSPACE_RGB;
    const RasterPage    page{.width = 2, .height = 1, .rgb = numbered_pixels(2), .color_space = color_space};
    return std::vector<RasterPage>(page_count, page);
}

void finish_format(const RasterFile& raster, RasterFormat format, std::size_t page_count) {
    constexpr std::size_t PIXEL_BYTES = 2 * 3;
    if (format == RasterFormat::VERSION_1) convert_to_version_1(raster.path(), page_count, PIXEL_BYTES);
}

void check_damaged_page_header(RasterFormat format, std::uintmax_t header_bytes_kept, std::string_view description) {
    const RasterFile one_page(pages_for(format, 1), write_mode(format));
    const RasterFile two_pages(pages_for(format, 2), write_mode(format));
    finish_format(one_page, format, 1);
    finish_format(two_pages, format, 2);
    std::filesystem::resize_file(two_pages.path(), std::filesystem::file_size(one_page.path()) + header_bytes_kept);

    const Settings settings = test_settings();
    PageConverter  converter(settings, nullptr);
    RasterReader   reader = two_pages.open();
    static_cast<void>(converted_page(reader, converter, 1));
    const PageHeaderResult second_header = reader.next_page();
    check(!second_header.has_value(), std::format("a page header cut short is an error ({})", description));

    RasterReader  complete_reader = one_page.open();
    PageConverter complete_converter(settings, nullptr);
    static_cast<void>(converted_page(complete_reader, complete_converter, 1));
    const PageHeaderResult end_of_input = complete_reader.next_page();
    check(
        end_of_input.has_value() && !end_of_input->has_value(),
        std::format("the end of a complete raster is not an error ({})", description)
    );
}

void a_damaged_page_header_is_an_error() {
    constexpr std::uintmax_t ALMOST_WHOLE_HEADER = sizeof(cups_page_header2_t) - 8;
    constexpr std::uintmax_t VERSION_1_HEADER_MISSING = sizeof(cups_page_header2_t) - sizeof(cups_page_header_t);
    check_damaged_page_header(RasterFormat::VERSION_3, 100, "uncompressed");
    check_damaged_page_header(RasterFormat::VERSION_1, 100, "version 1");
    check_damaged_page_header(RasterFormat::APPLE, 20, "Apple raster");
    check_damaged_page_header(RasterFormat::COMPRESSED, 100, "compressed");
    check_damaged_page_header(RasterFormat::COMPRESSED, ALMOST_WHOLE_HEADER, "compressed, 8 bytes missing");
    check_damaged_page_header(RasterFormat::COMPRESSED, VERSION_1_HEADER_MISSING, "compressed, a version 1 header's size missing");
}

void a_raster_at_another_resolution_is_rejected() {
    const RasterFile raster({
        {.width = 2, .height = 1, .rgb = numbered_pixels(2), .resolution = 300}
    });
    const Settings   settings = test_settings();
    PageConverter    converter(settings, nullptr);
    RasterReader     reader = raster.open();
    PageResult       page = convert_next(reader, converter, 1);
    check(!page.has_value() && page.error().contains("300x300 dpi"), "a 300 dpi raster is rejected in a 600 dpi job");
}

void bad_rasters_are_errors() {
    const RasterFile truncated({
        {.width = 2, .height = 1, .rgb = numbered_pixels(2), .declared_height = 3}
    });
    const Settings   settings = test_settings();
    PageConverter    converter(settings, nullptr);
    RasterReader     reader = truncated.open();
    auto             page = convert_next(reader, converter, 1);
    check(!page.has_value() && page.error().contains("ended early"), "a short raster is an error");

    const RasterFile gray({
        {.width = 2, .height = 1, .rgb = numbered_pixels(2), .color_space = CUPS_CSPACE_K}
    });
    RasterReader     gray_reader = gray.open();
    auto             unsupported = convert_next(gray_reader, converter, 1);
    check(!unsupported.has_value() && unsupported.error().contains("unsupported"), "a gray raster is rejected");
}

void geometry_rounds_like_the_vendor() {
    check_equal(printable_extent(11.28f, 600.72f, Resolution::DPI_600), 4912u);
    check_equal(printable_extent(11.28f, 780.72f, Resolution::DPI_300), 3206u);
    check_equal(printable_extent(0, 0.06f, Resolution::DPI_600), 1u);

    const Crop smaller = centered_crop({.width = 5100, .height = 6600}, {.width = 4912, .height = 6416});
    check_equal(smaller.left, 94u);
    check_equal(smaller.top, 92u);
    check_equal(smaller.width, 4912u);

    const Crop larger = centered_crop({.width = 100, .height = 100}, {.width = 200, .height = 200});
    check_equal(larger.left, 0u);
    check_equal(larger.width, 100u);
}

constexpr std::array TESTS = {
    TestCase{"page converter: rotation reverses lines and pixels",             rotation_reverses_lines_and_pixels            },
    TestCase{"page converter: long edge duplex turns back pages",              long_edge_duplex_turns_back_pages             },
    TestCase{"page converter: the crop is centered",                           the_crop_is_centered                          },
    TestCase{"page converter: an odd crop keeps the extra pixel on the right", an_odd_crop_keeps_the_extra_pixel_on_the_right},
    TestCase{"page converter: blank pages are skipped when asked",             blank_pages_are_skipped_when_asked            },
    TestCase{"page converter: the black table makes gray pages",               the_black_table_makes_gray_pages              },
    TestCase{"page converter: bad rasters are errors",                         bad_rasters_are_errors                        },
    TestCase{"page converter: a damaged page header is an error",              a_damaged_page_header_is_an_error             },
    TestCase{"page converter: a raster at another resolution is rejected",     a_raster_at_another_resolution_is_rejected    },
    TestCase{"page converter: geometry rounds like the vendor",                geometry_rounds_like_the_vendor               },
};

}

std::span<const TestCase> page_converter_tests() {
    return TESTS;
}

}
