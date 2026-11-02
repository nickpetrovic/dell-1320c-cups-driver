#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "print_format.hpp"

namespace dell1320c {

enum class DuplexMode : std::uint8_t {
    NONE,
    LONG_EDGE,
    SHORT_EDGE,
};

struct Paper {
    float        width = 0;
    float        length = 0;
    float        left = 0;
    float        bottom = 0;
    float        right = 0;
    float        top = 0;
    std::uint8_t code = 0;
    bool         custom = false;
    bool         long_edge_feed = false;
};

struct Tray {
    std::uint8_t     page_code = 0;
    std::uint8_t     job_info_code = 0;
    std::string_view pjl_name = {};
    bool             auto_switch = false;
};

struct ColorControls {
    int lookup_table = 0;
    int under_color_removal = 0;
    int tone_curve = 0;
    int screen = 0;
    int mono_tone_curve = 0;
    int mono_screen = 0;
};

struct Settings {
    Resolution       resolution = Resolution::DPI_600;
    int              copies = 1;
    bool             collate = false;
    ColorMode        color_mode = ColorMode::COLOR;
    bool             correction_off = false;
    bool             skip_blank_pages = false;
    bool             rotate = false;
    bool             toner_saver = false;
    bool             media_back_face = false;
    DuplexMode       duplex = DuplexMode::NONE;
    Tray             tray;
    Paper            paper;
    ColorControls    color;
    std::string      document_type;
    std::string_view media;
    std::string_view preferred_media;
    std::string_view substitute_tray;
};

[[nodiscard]] std::expected<Settings, std::string> load_settings(const std::string& ppd_path, const std::string& options, int copies);

}
