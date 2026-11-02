#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#include "settings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <span>
#include <utility>

#include <cups/cups.h>
#include <cups/ppd.h>

#include "unique_c_pointer.hpp"

namespace dell1320c {
namespace {

struct NamedValue {
    std::string_view name;
    std::string_view value;
};

struct NamedCode {
    std::string_view name;
    std::uint8_t     code;
};

constexpr auto PAPER_CODES = std::to_array<NamedCode>({
    {"Letter",              0x00},
    {"Legal",               0x01},
    {"A4",                  0x02},
    {"Executive",           0x03},
    {"Env10",               0x06},
    {"EnvMonarch",          0x07},
    {"FXEnvMonarchRotated", 0x07},
    {"EnvC5",               0x08},
    {"EnvDL",               0x09},
    {"FXEnvDLRotated",      0x09},
    {"B5",                  0x0b},
    {"FXPostcard",          0x0d},
    {"A5",                  0x0f},
    {"FanFoldGermanLegal",  0xcd},
    {"FXEnvYou2Rotated",    0xe0},
    {"FXEnvYou3Rotated",    0xe1},
    {"EnvChou3",            0xe4},
});

constexpr auto MEDIA_TYPES = std::to_array<NamedValue>({
    {"Default",       "NORMAL"      },
    {"Stationary",    "NORMAL"      },
    {"FinePaper",     "FINE"        },
    {"Postcard",      "PCMEDIA"     },
    {"Envelope",      "ENVELOPE"    },
    {"ThickPaper",    "THICK"       },
    {"ThickPaper2",   "THICK2"      },
    {"Transparency",  "TRANSPARENCY"},
    {"LabelPaper",    "LABEL"       },
    {"CoatingPaper1", "COAT1"       },
    {"CoatingPaper2", "COAT2"       },
    {"CoatingPaper3", "COAT3"       },
});

constexpr auto PREFERRED_MEDIA_TYPES = std::to_array<NamedValue>({
    {"Default",       "OFF"   },
    {"Stationary",    "NORMAL"},
    {"FinePaper",     "FINE"  },
    {"CoatingPaper1", "COAT1" },
});

constexpr auto SUBSTITUTE_TRAYS = std::to_array<NamedValue>({
    {"Policy0", "AUTO"},
    {"Policy2", "OFF" },
    {"SMH",     "SMH" },
});

constexpr std::string_view BACK_SIDE_SUFFIX = "Back";
constexpr std::string_view CUSTOM_PAPER_PREFIX = "Custom";
constexpr std::string_view DEFAULT_MEDIA = "NORMAL";
constexpr std::uint8_t     CUSTOM_PAPER_CODE = 0xff;
constexpr std::uint8_t     FALLBACK_PAPER_CODE = 0x02;
constexpr float            MAX_PAPER_EXTENT = 7200;

struct DocumentTypeControls {
    std::string_view name;
    ColorControls    controls;
};

constexpr ColorControls NORMAL_COLOR_CONTROLS = {
    .lookup_table = 24,
    .under_color_removal = 24,
    .tone_curve = 9,
    .screen = 9,
    .mono_tone_curve = 59,
    .mono_screen = 59,
};

constexpr auto DOCUMENT_TYPE_CONTROLS = std::to_array<DocumentTypeControls>({
    {"Normal",       NORMAL_COLOR_CONTROLS                                                                                                    },
    {"Photo",        {.lookup_table = 22, .under_color_removal = 22, .tone_curve = 11, .screen = 11, .mono_tone_curve = 59, .mono_screen = 59}},
    {"Presentation",
     {.lookup_table = 26, .under_color_removal = 26, .tone_curve = 13, .screen = 13, .mono_tone_curve = 59, .mono_screen = 59}                },
    {"Web",          {.lookup_table = 23, .under_color_removal = 23, .tone_curve = 9, .screen = 9, .mono_tone_curve = 59, .mono_screen = 59}  },
    {"POP",          {.lookup_table = 32, .under_color_removal = 32, .tone_curve = 9, .screen = 9, .mono_tone_curve = 59, .mono_screen = 59}  },
    {"Off",          NORMAL_COLOR_CONTROLS                                                                                                    },
});

struct OptionsDeleter {
    int count = 0;

    void operator()(cups_option_t* options) const noexcept {
        cupsFreeOptions(count, options);
    }
};

using PpdFile = UniqueCPointer<ppd_file_t, ppdClose>;
using ParsedOptions = std::unique_ptr<cups_option_t, OptionsDeleter>;

std::string_view lookup(std::span<const NamedValue> table, std::string_view name, std::string_view fallback) {
    const auto entry = std::ranges::find(table, name, &NamedValue::name);
    return entry == table.end() ? fallback : entry->value;
}

std::string_view marked_choice(ppd_file_t* ppd, const char* keyword) {
    const ppd_choice_t* choice = ppdFindMarkedChoice(ppd, keyword);
    return choice ? std::string_view(choice->choice) : std::string_view();
}

Tray tray_for_slot(std::string_view slot) {
    if (slot == "BypassTray") return {.page_code = 1, .job_info_code = 5};
    if (slot == "1stTray-S" || slot == "1stTray-H") return {.page_code = 2, .job_info_code = 1, .pjl_name = "INTRAY1"};
    if (slot == "2ndTray-H") return {.page_code = 3, .job_info_code = 2, .pjl_name = "INTRAY2"};
    return {.auto_switch = true};
}

DuplexMode duplex_mode(std::string_view choice) {
    if (choice == "DuplexNoTumble") return DuplexMode::LONG_EDGE;
    if (choice == "DuplexTumble") return DuplexMode::SHORT_EDGE;
    return DuplexMode::NONE;
}

void load_media(Settings& settings, ppd_file_t* ppd) {
    std::string_view media = marked_choice(ppd, "MediaType");
    settings.media_back_face = media.size() > BACK_SIDE_SUFFIX.size() && media.ends_with(BACK_SIDE_SUFFIX);
    if (settings.media_back_face) media.remove_suffix(BACK_SIDE_SUFFIX.size());
    settings.media = lookup(MEDIA_TYPES, media, DEFAULT_MEDIA);
}

bool has_valid_geometry(const Paper& paper) {
    const std::array values = {paper.width, paper.length, paper.left, paper.bottom, paper.right, paper.top};
    const bool       finite = std::ranges::all_of(values, [](float value) { return std::isfinite(value); });
    const bool       within_extent = paper.width <= MAX_PAPER_EXTENT && paper.length <= MAX_PAPER_EXTENT;
    const bool       horizontal = 0 <= paper.left && paper.left < paper.right && paper.right <= paper.width;
    const bool       vertical = 0 <= paper.bottom && paper.bottom < paper.top && paper.top <= paper.length;
    return finite && within_extent && horizontal && vertical;
}

std::expected<Paper, std::string> load_paper(ppd_file_t* ppd) {
    const ppd_size_t* size = ppdPageSize(ppd, nullptr);
    if (!size) return std::unexpected(std::string("the PPD has no paper size for this job"));

    const std::string_view name = size->name;
    Paper                  paper{
        .width = size->width,
        .length = size->length,
        .left = size->left,
        .bottom = size->bottom,
        .right = size->right,
        .top = size->top,
        .code = FALLBACK_PAPER_CODE,
        .long_edge_feed = size->width >= size->length,
    };

    if (name.starts_with(CUSTOM_PAPER_PREFIX)) {
        paper.code = CUSTOM_PAPER_CODE;
        paper.custom = true;
    } else if (const auto entry = std::ranges::find(PAPER_CODES, name, &NamedCode::name); entry != PAPER_CODES.end()) {
        paper.code = entry->code;
    }
    if (paper.custom) {
        ppd_size_t minimum{};
        ppd_size_t maximum{};
        const bool has_limits = ppdPageSizeLimits(ppd, &minimum, &maximum) != 0;
        const bool within_maximum = has_limits && paper.width <= maximum.width && paper.length <= maximum.length;
        const bool has_printable_area = paper.right > paper.left && paper.top > paper.bottom;
        if (!within_maximum || !has_printable_area)
            return std::unexpected(
                std::format("the custom paper size {:g}x{:g} points is outside the printer's limits", paper.width, paper.length)
            );
    }
    if (!has_valid_geometry(paper))
        return std::unexpected(std::format("the PPD gives the paper size {} an invalid size or printable area", name));
    return paper;
}

void load_color(Settings& settings, ppd_file_t* ppd) {
    const std::string_view document_type = marked_choice(ppd, "FXDocumentType");
    const auto             entry = std::ranges::find(DOCUMENT_TYPE_CONTROLS, document_type, &DocumentTypeControls::name);
    const bool             known = entry != DOCUMENT_TYPE_CONTROLS.end();

    settings.document_type = known ? entry->name : "Normal";
    settings.correction_off = settings.document_type == "Off";
    settings.color_mode = marked_choice(ppd, "FXColorMode") == "Black" ? ColorMode::GRAY : ColorMode::COLOR;
    settings.color = known ? entry->controls : NORMAL_COLOR_CONTROLS;
}

}

std::expected<Settings, std::string> load_settings(const std::string& ppd_path, const std::string& options, int copies) {
    const PpdFile ppd(ppdOpenFile(ppd_path.c_str()));
    if (!ppd) return std::unexpected(std::format("unable to open PPD file {}", ppd_path));

    cups_option_t*      parsed_options = nullptr;
    const int           parsed_count = cupsParseOptions(options.c_str(), 0, &parsed_options);
    const ParsedOptions owned_options(parsed_options, OptionsDeleter{parsed_count});
    ppdMarkDefaults(ppd.get());
    if (parsed_count > 0) cupsMarkOptions(ppd.get(), parsed_count, parsed_options);

    Settings settings;
    settings.copies = std::max(copies, 1);
    settings.collate = marked_choice(ppd.get(), "Collate") == "True";
    settings.resolution = marked_choice(ppd.get(), "FXOutputMode") == "300x300" ? Resolution::DPI_300 : Resolution::DPI_600;
    settings.skip_blank_pages = marked_choice(ppd.get(), "FXSkip") == "True";
    settings.rotate = marked_choice(ppd.get(), "FXTurnPage") == "True";
    settings.toner_saver = marked_choice(ppd.get(), "FXTonerSaver") == "True";
    settings.duplex = duplex_mode(marked_choice(ppd.get(), "Duplex"));
    settings.tray = tray_for_slot(marked_choice(ppd.get(), "FXInputSlot"));
    auto paper = load_paper(ppd.get());
    if (!paper) return std::unexpected(std::move(paper.error()));
    settings.paper = *paper;
    load_color(settings, ppd.get());
    load_media(settings, ppd.get());
    settings.preferred_media = lookup(PREFERRED_MEDIA_TYPES, marked_choice(ppd.get(), "FXPreferredMediaType"), "OFF");
    settings.substitute_tray = lookup(SUBSTITUTE_TRAYS, marked_choice(ppd.get(), "FXSubPaperSelection"), "AUTO");
    return settings;
}

}
