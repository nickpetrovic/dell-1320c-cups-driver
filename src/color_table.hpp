#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "print_format.hpp"
#include "settings.hpp"

namespace dell1320c {

struct ColorTableChoice {
    std::string name;
    ColorMode   output;
};

[[nodiscard]] std::optional<ColorTableChoice> color_table_for(const Settings& settings);

class ColorTable {
public:
    [[nodiscard]] static std::expected<ColorTable, std::string> load(
        const std::filesystem::path& directory, std::string_view name, ColorMode output
    );

    [[nodiscard]] ColorMode output_mode() const noexcept {
        return output;
    }

    [[nodiscard]] std::span<const std::uint8_t> map_line(std::span<const std::uint8_t> rgb, std::span<std::uint8_t> mapped) const;

private:
    ColorTable(ColorMode output_mode, std::unique_ptr<std::uint8_t[]> decoded_entries) noexcept;

    ColorMode                       output;
    std::unique_ptr<std::uint8_t[]> entries;
};

}
