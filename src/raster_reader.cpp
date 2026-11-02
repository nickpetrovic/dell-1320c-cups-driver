#include "raster_reader.hpp"

#include <algorithm>
#include <cerrno>
#include <memory>
#include <format>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include "print_format.hpp"
#include "sq21.hpp"

namespace dell1320c {
namespace {

constexpr std::size_t SYNC_WORD_SIZE = 4;
constexpr std::size_t APPLE_PAGE_HEADER_SIZE = 32;

}

bool is_supported_raster(const cups_page_header2_t& header) {
    const bool rgb = header.cupsColorSpace == CUPS_CSPACE_RGB || header.cupsColorSpace == CUPS_CSPACE_SRGB;
    const bool eight_bit_chunky =
        header.cupsBitsPerColor == 8 && header.cupsBitsPerPixel == 24 && header.cupsColorOrder == CUPS_ORDER_CHUNKED;
    const bool width_in_range = header.cupsWidth > 0 && header.cupsWidth <= sq21::MAX_LINE_WIDTH;
    const bool whole_line = header.cupsBytesPerLine == std::uint64_t{header.cupsWidth} * RGB_CHANNELS;
    return rgb && eight_bit_chunky && width_in_range && whole_line;
}

RasterReader::RasterReader(std::unique_ptr<Input> raster_input, RasterStream raster_stream) noexcept
    : input(std::move(raster_input)), raster(std::move(raster_stream)) {}

ssize_t RasterReader::read_input(void* context, unsigned char* buffer, std::size_t length) {
    auto*   source = static_cast<Input*>(context);
    ssize_t count = 0;
    do {
        count = ::read(source->descriptor, buffer, length);
    } while (count < 0 && errno == EINTR);
    if (count == 0) {
        source->end_of_input = true;
        source->end_of_input_request = length;
    }
    if (count > 0) {
        source->delivered_bytes += static_cast<std::uint64_t>(count);
        const std::size_t missing_sync_bytes = SYNC_WORD_SIZE - source->sync_word.size();
        const std::size_t sync_bytes = std::min(missing_sync_bytes, static_cast<std::size_t>(count));
        source->sync_word.append(reinterpret_cast<const char*>(buffer), sync_bytes);
    }
    return count;
}

std::size_t RasterReader::page_header_size() const noexcept {
    if (input->sync_word == "RaSt" || input->sync_word == "tSaR") return sizeof(cups_page_header_t);
    if (input->sync_word == "UNIR" || input->sync_word == "RINU") return APPLE_PAGE_HEADER_SIZE;
    return sizeof(cups_page_header2_t);
}

std::expected<RasterReader, std::string> RasterReader::open(const char* path) {
    auto source = std::make_unique<Input>();
    source->descriptor = STDIN_FILENO;

    if (path) {
        const int descriptor = ::open(path, O_RDONLY | O_CLOEXEC);
        if (descriptor < 0) return std::unexpected(std::format("unable to open {}: {}", path, std::generic_category().message(errno)));
        source->owned_file = FileDescriptor(descriptor);
        source->descriptor = descriptor;
    }

    RasterStream raster(cupsRasterOpenIO(read_input, source.get(), CUPS_RASTER_READ));
    if (!raster) return std::unexpected(std::string("unable to read the CUPS raster stream"));
    return RasterReader(std::move(source), std::move(raster));
}

PageHeaderResult RasterReader::next_page() {
    const std::uint64_t delivered_before = input->delivered_bytes;
    cups_page_header2_t header{};
    if (cupsRasterReadHeader2(raster.get(), &header) != 0) return header;

    const bool nothing_after_last_page = input->delivered_bytes == delivered_before;
    const bool asked_for_whole_header = input->end_of_input_request == page_header_size();
    const bool clean_end = input->end_of_input && nothing_after_last_page && asked_for_whole_header;
    if (clean_end) return std::nullopt;
    return std::unexpected(std::string("the raster data has an incomplete or invalid page header"));
}

bool RasterReader::read_line(std::span<std::uint8_t> line) {
    const auto size = static_cast<unsigned>(line.size());
    return cupsRasterReadPixels(raster.get(), line.data(), size) == size;
}

}
