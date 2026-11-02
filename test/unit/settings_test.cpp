#include <array>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

#include "color_table.hpp"
#include "settings.hpp"
#include "testing.hpp"

namespace dell1320c::testing {
namespace {

std::string test_ppd() {
    const char* path = std::getenv("DELL1320C_TEST_PPD");
    if (!path || !*path) throw std::runtime_error("set DELL1320C_TEST_PPD to the generated PPD");
    return path;
}

Settings load(const std::string& options, int copies = 1) {
    auto settings = load_settings(test_ppd(), options, copies);
    if (!settings) throw std::runtime_error(settings.error());
    return *settings;
}

void defaults_come_from_the_ppd() {
    const Settings settings = load("");

    check_equal(settings.resolution, Resolution::DPI_600);
    check_equal(settings.copies, 1);
    check(!settings.collate, "collation is off");
    check_equal(settings.color_mode, ColorMode::COLOR);
    check(settings.duplex == DuplexMode::NONE, "duplex is off");
    check_equal(settings.tray.page_code, 2);
    check_equal(settings.tray.job_info_code, 1);
    check_equal(settings.tray.pjl_name, "INTRAY1");
    check(!settings.tray.auto_switch, "tray 1 does not switch trays");
    check_equal(settings.paper.code, 0x02);
    check_equal(settings.paper.width, 595.0f);
    check_equal(settings.paper.length, 842.0f);
    check(!settings.paper.long_edge_feed, "A4 feeds short edge first");
    check_equal(settings.document_type, "Normal");
    check_equal(settings.color.lookup_table, 24);
    check_equal(settings.color.tone_curve, 9);
    check_equal(settings.color.mono_screen, 59);
    check_equal(settings.media, "NORMAL");
    check_equal(settings.preferred_media, "OFF");
    check_equal(settings.substitute_tray, "AUTO");
}

void options_override_the_defaults() {
    const std::string print_options = "PageSize=Letter FXColorMode=Black FXDocumentType=Photo Duplex=DuplexNoTumble FXOutputMode=300x300";
    const std::string paper_options = "FXInputSlot=BypassTray MediaType=ThickPaperBack Collate=True FXTonerSaver=True";
    const Settings    settings = load(print_options + " " + paper_options, 3);

    check_equal(settings.resolution, Resolution::DPI_300);
    check_equal(settings.copies, 3);
    check(settings.collate, "collation is on");
    check_equal(settings.color_mode, ColorMode::GRAY);
    check(settings.toner_saver, "toner saver is on");
    check(settings.duplex == DuplexMode::LONG_EDGE, "long edge duplex");
    check_equal(settings.tray.page_code, 1);
    check_equal(settings.tray.job_info_code, 5);
    check(settings.tray.pjl_name.empty(), "the bypass tray has no PJL tray name");
    check_equal(settings.paper.code, 0x00);
    check_equal(settings.document_type, "Photo");
    check_equal(settings.color.lookup_table, 22);
    check_equal(settings.color.screen, 11);
    check_equal(settings.media, "THICK");
    check(settings.media_back_face, "a Back media type prints on the back face");
}

void custom_and_landscape_sizes() {
    const Settings custom = load("PageSize=Custom.612x1008");
    check(custom.paper.custom, "custom size");
    check_equal(custom.paper.code, 0xff);
    check_equal(custom.paper.length, 1008.0f);

    const Settings landscape = load("PageSize=FXEnvDLRotated");
    check_equal(landscape.paper.code, 0x09);
    check(landscape.paper.long_edge_feed, "a landscape envelope feeds long edge first");

    const Settings square = load("PageSize=Custom.500x500");
    check(square.paper.long_edge_feed, "a square sheet feeds long edge first");

    const Settings region = load("PageRegion=Letter");
    check_equal(region.paper.code, 0x00);
    check_equal(region.paper.width, 612.0f);
}

void paper_sizes_the_printer_cannot_use_are_errors() {
    const auto too_large = load_settings(test_ppd(), "PageSize=Custom.700x1100", 1);
    check(
        !too_large.has_value() && too_large.error().contains("outside the printer's limits"), "a custom size above the maximum is an error"
    );

    const auto no_printable_area = load_settings(test_ppd(), "PageSize=Custom.10x10", 1);
    check(!no_printable_area.has_value(), "a custom size smaller than the margins is an error");

    const std::string original = read_file(test_ppd());
    const std::string a4_area = "*ImageableArea A4/A4: \"11.28 11.28 583.72 830.72\"";
    const std::size_t position = original.find(a4_area);
    check(position != std::string::npos, "the test PPD has the A4 imageable area");
    if (position == std::string::npos) return;

    std::string damaged = original;
    damaged.replace(position, a4_area.size(), "*ImageableArea A4/A4: \"0 0 1000000000 842\"");
    const TemporaryDirectory directory;
    const std::string        damaged_path = (directory.path() / "damaged.ppd").string();
    std::ofstream(damaged_path, std::ios::binary) << damaged;

    const auto damaged_settings = load_settings(damaged_path, "PageSize=A4", 1);
    check(!damaged_settings.has_value() && damaged_settings.error().contains("A4"), "an impossible printable area in the PPD is an error");
}

void only_black_output_color_prints_in_gray() {
    check_equal(load("FXColorMode=Black").color_mode, ColorMode::GRAY);
    check_equal(load("FXColorMode=Color").color_mode, ColorMode::COLOR);
}

void the_color_table_follows_the_color_settings() {
    std::optional<ColorTableChoice>       normal = color_table_for(load(""));
    std::optional<ColorTableChoice>       photo = color_table_for(load("FXDocumentType=Photo"));
    std::optional<ColorTableChoice>       black = color_table_for(load("FXColorMode=Black"));
    std::optional<ColorTableChoice>       black_off = color_table_for(load("FXColorMode=Black FXDocumentType=Off"));
    const std::optional<ColorTableChoice> color_off = color_table_for(load("FXDocumentType=Off"));

    check(normal && normal->name == "Normal" && normal->output == ColorMode::COLOR, "Normal uses the Normal color table");
    check(photo && photo->name == "Photo" && photo->output == ColorMode::COLOR, "Photo uses the Photo color table");
    check(black && black->name == "Black" && black->output == ColorMode::GRAY, "black output uses the Black table");
    check(black_off && black_off->name == "BlackOff" && black_off->output == ColorMode::GRAY, "black with correction off uses BlackOff");
    check(!color_off, "color with correction off uses no table");
}

void color_correction_off_and_tray_policies() {
    const Settings settings = load("FXDocumentType=Off FXInputSlot=AutoSelect FXSubPaperSelection=Policy2 FXPreferredMediaType=FinePaper");

    check(settings.correction_off, "color correction is off");
    check(settings.tray.auto_switch, "automatic tray selection switches trays");
    check_equal(settings.tray.page_code, 0);
    check_equal(settings.substitute_tray, "OFF");
    check_equal(settings.preferred_media, "FINE");
}

void copies_are_at_least_one() {
    check_equal(load("", 0).copies, 1);
    check_equal(load("", -4).copies, 1);
}

void a_missing_ppd_is_an_error() {
    auto settings = load_settings("/nonexistent/dell1320c.ppd", "", 1);
    check(!settings.has_value(), "loading fails");
    if (!settings) check(settings.error().contains("/nonexistent/dell1320c.ppd"), "the error names the file");
}

constexpr std::array TESTS = {
    TestCase{"settings: defaults come from the PPD",                    defaults_come_from_the_ppd                   },
    TestCase{"settings: options override the defaults",                 options_override_the_defaults                },
    TestCase{"settings: custom and landscape paper sizes",              custom_and_landscape_sizes                   },
    TestCase{"settings: paper sizes the printer cannot use are errors", paper_sizes_the_printer_cannot_use_are_errors},
    TestCase{"settings: color correction off and tray rules",           color_correction_off_and_tray_policies       },
    TestCase{"settings: copies are at least one",                       copies_are_at_least_one                      },
    TestCase{"settings: a missing PPD is an error",                     a_missing_ppd_is_an_error                    },
    TestCase{"settings: only black output prints in gray",              only_black_output_color_prints_in_gray       },
    TestCase{"settings: the color table follows the color settings",    the_color_table_follows_the_color_settings   },
};

}

std::span<const TestCase> settings_tests() {
    return TESTS;
}

}
