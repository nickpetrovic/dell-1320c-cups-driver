#include <array>
#include <stdexcept>
#include <string>
#include <utility>

#include <fcntl.h>

#include "file_descriptor.hpp"
#include "raster_reader.hpp"
#include "sq21.hpp"
#include "testing.hpp"

namespace dell1320c::testing {
namespace {

cups_page_header2_t rgb_header(std::uint32_t width) {
    cups_page_header2_t header{};
    header.cupsWidth = width;
    header.cupsHeight = 1;
    header.cupsBitsPerColor = 8;
    header.cupsBitsPerPixel = 24;
    header.cupsBytesPerLine = width * 3;
    header.cupsColorOrder = CUPS_ORDER_CHUNKED;
    header.cupsColorSpace = CUPS_CSPACE_SRGB;
    return header;
}

void eight_bit_rgb_lines_are_supported() {
    check(is_supported_raster(rgb_header(5100)), "a Letter-width sRGB page is supported");
    cups_page_header2_t rgb = rgb_header(8);
    rgb.cupsColorSpace = CUPS_CSPACE_RGB;
    check(is_supported_raster(rgb), "plain RGB is supported");
}

void malformed_headers_are_rejected() {
    const cups_page_header2_t no_width = rgb_header(0);
    const cups_page_header2_t too_wide = rgb_header(sq21::MAX_LINE_WIDTH + 1);
    cups_page_header2_t       padded_line = rgb_header(8);
    padded_line.cupsBytesPerLine = 8 * 3 + 1;
    cups_page_header2_t short_line = rgb_header(8);
    short_line.cupsBytesPerLine = 8 * 3 - 1;
    cups_page_header2_t four_bytes_per_pixel = rgb_header(8);
    four_bytes_per_pixel.cupsBitsPerPixel = 32;
    cups_page_header2_t sixteen_bit = rgb_header(8);
    sixteen_bit.cupsBitsPerColor = 16;
    cups_page_header2_t planar = rgb_header(8);
    planar.cupsColorOrder = CUPS_ORDER_PLANAR;
    cups_page_header2_t black = rgb_header(8);
    black.cupsColorSpace = CUPS_CSPACE_K;

    check(!is_supported_raster(no_width), "a page with no width is rejected");
    check(!is_supported_raster(too_wide), "a page wider than the compressor's limit is rejected");
    check(!is_supported_raster(padded_line), "lines with extra bytes are rejected");
    check(!is_supported_raster(short_line), "lines shorter than the width are rejected");
    check(!is_supported_raster(four_bytes_per_pixel), "a header that claims 32-bit pixels is rejected");
    check(!is_supported_raster(sixteen_bit), "16 bits per color are rejected");
    check(!is_supported_raster(planar), "planar color is rejected");
    check(!is_supported_raster(black), "a black-only raster is rejected");
}

void a_missing_raster_file_is_an_error() {
    auto reader = RasterReader::open("/nonexistent/input.ras");
    check(!reader.has_value(), "opening fails");
    if (!reader) check(reader.error().contains("/nonexistent/input.ras"), "the error names the file");
}

void file_descriptors_close_once() {
    const int raw_descriptor = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (raw_descriptor < 0) throw std::runtime_error("unable to open /dev/null");
    {
        FileDescriptor       first(raw_descriptor);
        const FileDescriptor second(std::move(first));
        check_equal(first.get(), -1);
        check_equal(second.get(), raw_descriptor);
        check(::fcntl(raw_descriptor, F_GETFD) != -1, "the descriptor stays open while owned");
    }
    check(::fcntl(raw_descriptor, F_GETFD) == -1, "the descriptor is closed when its owner goes away");
}

void move_assignment_closes_the_old_descriptor() {
    const int old_descriptor = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    const int new_descriptor = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (old_descriptor < 0 || new_descriptor < 0) throw std::runtime_error("unable to open /dev/null");

    FileDescriptor owner(old_descriptor);
    owner = FileDescriptor(new_descriptor);
    check(::fcntl(old_descriptor, F_GETFD) == -1, "the replaced descriptor is closed");
    check_equal(owner.get(), new_descriptor);
}

constexpr std::array TESTS = {
    TestCase{"raster reader: eight-bit RGB lines are supported",           eight_bit_rgb_lines_are_supported        },
    TestCase{"raster reader: malformed headers are rejected",              malformed_headers_are_rejected           },
    TestCase{"raster reader: a missing raster file is an error",           a_missing_raster_file_is_an_error        },
    TestCase{"file descriptor: closes when its owner goes away",           file_descriptors_close_once              },
    TestCase{"file descriptor: move assignment closes the old descriptor", move_assignment_closes_the_old_descriptor},
};

}

std::span<const TestCase> raster_reader_tests() {
    return TESTS;
}

}
