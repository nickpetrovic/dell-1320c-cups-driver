#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "print_format.hpp"

namespace dell1320c::sq21 {

constexpr std::uint32_t MAX_RUN_LENGTH = 17057;
constexpr std::uint32_t MAX_LINE_WIDTH = 1u << 16;

class Compressor {
public:
    Compressor(ColorMode mode, std::uint32_t width, Resolution resolution);
    Compressor(const Compressor&) = delete;
    Compressor& operator=(const Compressor&) = delete;
    Compressor(Compressor&&) noexcept;
    Compressor& operator=(Compressor&&) noexcept;
    ~Compressor();

    void add_line(std::span<const std::uint8_t> line);
    [[nodiscard]] std::vector<std::uint8_t> finish() &&;

private:
    class Encoder;

    std::unique_ptr<Encoder> encoder;
};

constexpr std::uint32_t padded_width(std::uint32_t width) noexcept {
    return (width + 7) & ~7u;
}

[[nodiscard]] std::vector<std::uint8_t> compress(
    ColorMode mode, std::span<const std::uint8_t> pixels, std::uint32_t width, Resolution resolution
);

}
