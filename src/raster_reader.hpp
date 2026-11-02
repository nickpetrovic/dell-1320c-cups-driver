#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include <sys/types.h>

#include <cups/raster.h>

#include "file_descriptor.hpp"
#include "unique_c_pointer.hpp"

namespace dell1320c {

using PageHeaderResult = std::expected<std::optional<cups_page_header2_t>, std::string>;

[[nodiscard]] bool is_supported_raster(const cups_page_header2_t& header);

class RasterReader {
public:
    [[nodiscard]] static std::expected<RasterReader, std::string> open(const char* path);

    [[nodiscard]] PageHeaderResult next_page();
    [[nodiscard]] bool read_line(std::span<std::uint8_t> line);

private:
    struct Input {
        FileDescriptor owned_file;
        int            descriptor = -1;
        std::uint64_t  delivered_bytes = 0;
        std::size_t    end_of_input_request = 0;
        bool           end_of_input = false;
        std::string    sync_word;
    };

    [[nodiscard]] std::size_t page_header_size() const noexcept;

    using RasterStream = UniqueCPointer<cups_raster_t, cupsRasterClose>;

    static ssize_t read_input(void* context, unsigned char* buffer, std::size_t length);

    RasterReader(std::unique_ptr<Input> raster_input, RasterStream raster_stream) noexcept;

    std::unique_ptr<Input> input;
    RasterStream           raster;
};

}
