#include "hbpl.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <print>
#include <system_error>
#include <utility>

#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "sq21.hpp"
#include "unique_c_pointer.hpp"

namespace dell1320c::hbpl {
namespace {

constexpr std::string_view UNIVERSAL_EXIT_LANGUAGE = "\033%-12345X";
constexpr std::string_view FALLBACK_HOST_NAME = "localhost";

enum class RecordType : std::uint8_t {
    BEGIN_DOCUMENT = 0x41,
    END_DOCUMENT = 0x42,
    BEGIN_PAGE = 0x43,
    END_PAGE = 0x44,
    BEGIN_IMAGE = 0x51,
    IMAGE_DATA = 0x52,
    END_IMAGE = 0x53,
};

enum class ValueType : std::uint8_t {
    BYTE = 0xa1,
    WORD = 0xa2,
    LONG = 0xa4,
    DATA_LENGTH = 0xb1,
    WORD_PAIR = 0xc2,
    SHORT_PAIR = 0xc3,
    LONG_PAIR = 0xc4,
};

enum class Attribute : std::uint8_t {
    UNKNOWN_81 = 0x81,
    UNKNOWN_82 = 0x82,
    UNKNOWN_83 = 0x83,
    LONG_EDGE_FEED = 0x91,
    TRAY = 0x92,
    UNKNOWN_93 = 0x93,
    PAPER_SIZE = 0x94,
    CUSTOM_PAPER_SIZE = 0x95,
    UNKNOWN_96 = 0x96,
    MARGINS = 0x97,
    DUPLEX_SIDE = 0x98,
    PAGE_NUMBER = 0x99,
    IMAGE_SIZE = 0x9a,
    UNKNOWN_9B = 0x9b,
    UNKNOWN_9C = 0x9c,
    COMPRESSION = 0x9d,
    UNKNOWN_9E = 0x9e,
    UNKNOWN_9F = 0x9f,
    BITS_PER_PIXEL = 0xa0,
    RESOLUTION = 0xa1,
    IMAGE_SIZE_COPY = 0xa2,
    UNKNOWN_A3 = 0xa3,
    DATA_LENGTH = 0xa4,
};

constexpr std::size_t   PAGE_RECORD_CAPACITY = 128;
constexpr std::uint32_t BAND_HEADER_SIZE = 24;
constexpr std::uint8_t  BAND_FORMAT = 1;
constexpr std::uint32_t INITIAL_PREDICTOR_ORDER = 0x00043210;
constexpr std::uint32_t BAND_UNKNOWN_FIELD = 0x00ff0000;
constexpr std::uint8_t  RESOLUTION_300_DPI = 3;
constexpr std::uint8_t  RESOLUTION_600_DPI = 0;
constexpr std::uint8_t  COLOR_COMPRESSION = 0x16;
constexpr std::uint8_t  MONO_COMPRESSION = 0x11;
constexpr std::uint8_t  COLOR_BITS = 0x20;
constexpr std::uint8_t  MONO_BITS = 0x08;
constexpr std::uint8_t  NOT_DUPLEX = 0;
constexpr std::uint8_t  FRONT_SIDE = 1;
constexpr std::uint8_t  BACK_SIDE = 2;
constexpr double        MILLIMETERS_PER_INCH = 25.4;

constexpr std::array<std::string_view, 3> PLANE_PREFIXES = {"T", "G", "I"};

class RecordWriter {
public:
    RecordWriter() {
        bytes.reserve(PAGE_RECORD_CAPACITY);
    }

    void begin(RecordType type) {
        put_u8(std::to_underlying(type));
    }

    void byte_attribute(Attribute attribute, std::uint8_t value) {
        put_header(attribute, ValueType::BYTE);
        put_u8(value);
    }

    void word_attribute(Attribute attribute, std::uint16_t value) {
        put_header(attribute, ValueType::WORD);
        put_u16(value);
    }

    void long_attribute(Attribute attribute, std::uint32_t value) {
        put_header(attribute, ValueType::LONG);
        put_u32(value);
    }

    void word_pair_attribute(Attribute attribute, std::uint16_t first, std::uint16_t second) {
        put_header(attribute, ValueType::WORD_PAIR);
        put_u16(first);
        put_u16(second);
    }

    void short_pair_attribute(Attribute attribute, std::uint16_t first, std::uint16_t second) {
        put_header(attribute, ValueType::SHORT_PAIR);
        put_u16(first);
        put_u16(second);
    }

    void long_pair_attribute(Attribute attribute, std::uint32_t first, std::uint32_t second) {
        put_header(attribute, ValueType::LONG_PAIR);
        put_u32(first);
        put_u32(second);
    }

    void data_length_attribute(std::uint32_t length) {
        put_header(Attribute::DATA_LENGTH, ValueType::DATA_LENGTH);
        put_u8(std::to_underlying(ValueType::LONG));
        put_u32(length);
    }

    void put_u8(std::uint8_t value) {
        bytes.push_back(value);
    }

    void put_u16(std::uint16_t value) {
        put_u8(static_cast<std::uint8_t>(value & 0xff));
        put_u8(static_cast<std::uint8_t>(value >> 8));
    }

    void put_u32(std::uint32_t value) {
        put_u16(static_cast<std::uint16_t>(value & 0xffff));
        put_u16(static_cast<std::uint16_t>(value >> 16));
    }

    [[nodiscard]] std::span<const std::uint8_t> view() const {
        return bytes;
    }

private:
    void put_header(Attribute attribute, ValueType type) {
        put_u8(std::to_underlying(attribute));
        put_u8(std::to_underlying(type));
    }

    std::vector<std::uint8_t> bytes;
};

std::string_view on_off(bool enabled) {
    return enabled ? "ON" : "OFF";
}

std::string_view duplex_type(DuplexMode duplex) {
    switch (duplex) {
    case DuplexMode::NONE: return "SIMPLEX";
    case DuplexMode::LONG_EDGE: return "DUPLEX";
    case DuplexMode::SHORT_EDGE: return "TUMBLE";
    }
    std::unreachable();
}

double to_millimeters(float points) {
    return static_cast<double>(points) * MILLIMETERS_PER_INCH / static_cast<double>(POINTS_PER_INCH);
}

std::uint16_t to_pixels(float points, float resolution) {
    return static_cast<std::uint16_t>(points * resolution / POINTS_PER_INCH + 0.5f);
}

std::uint16_t margin(float paper_extent, std::uint32_t image_extent, float imageable_margin, float resolution) {
    const std::uint16_t margin_pixels = to_pixels(imageable_margin, resolution);
    const float         paper_pixels = paper_extent * resolution / POINTS_PER_INCH;
    const float         used_pixels = static_cast<float>(image_extent) + static_cast<float>(margin_pixels);
    return used_pixels <= paper_pixels ? margin_pixels : std::uint16_t{0};
}

std::string first_ipv4_address() {
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) return {};

    const UniqueCPointer<ifaddrs, freeifaddrs> owned_interfaces(interfaces);
    for (const ifaddrs* entry = interfaces; entry; entry = entry->ifa_next) {
        const bool is_ipv4 = entry->ifa_addr && entry->ifa_addr->sa_family == AF_INET;
        const bool is_up = (entry->ifa_flags & IFF_UP) != 0 && (entry->ifa_flags & IFF_LOOPBACK) == 0;
        if (!is_ipv4 || !is_up) continue;

        sockaddr_in ipv4{};
        std::memcpy(&ipv4, entry->ifa_addr, sizeof(ipv4));
        std::array<std::uint8_t, 4> octets{};
        std::memcpy(octets.data(), &ipv4.sin_addr, octets.size());
        return host_address_text(octets);
    }
    return {};
}

}

std::string host_address_text(const std::array<std::uint8_t, 4>& octets) {
    return std::format("I{:X}{:X}{:X}{:X}", octets[0], octets[1], octets[2], octets[3]);
}

HostIdentity host_identity() {
    std::array<char, 256> name{};
    HostIdentity          identity;

    if (gethostname(name.data(), name.size() - 1) == 0)
        identity.name = printable_text(name.data());
    else
        identity.name = FALLBACK_HOST_NAME;
    identity.address = first_ipv4_address();
    return identity;
}

std::string printable_text(std::string_view text) {
    const auto breaks_pjl_string = [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte < 0x20 || byte == 0x7f || character == '"';
    };
    std::string printable(text);
    std::ranges::replace_if(printable, breaks_pjl_string, ' ');
    return printable;
}

JobWriter::JobWriter(std::FILE* stream, const Settings& print_settings, Job print_job)
    : output(stream), settings(print_settings), job(std::move(print_job)) {}

int JobWriter::sets() const {
    return settings.collate ? settings.copies : 1;
}

void JobWriter::begin() {
    RecordWriter document_begin;
    document_begin.begin(RecordType::BEGIN_DOCUMENT);
    document_begin.byte_attribute(Attribute::UNKNOWN_81, 0);
    document_begin.word_attribute(Attribute::UNKNOWN_82, 1);
    document_begin.word_attribute(Attribute::UNKNOWN_83, 1);

    write_header();
    write_bytes(document_begin.view());
}

void JobWriter::add_page(Page page) {
    const std::uint32_t position_in_set = page_number + 1;
    page_color_modes.push_back(page.color_mode);
    write_page(page, position_in_set);
    flush();
    if (sets() > 1) repeated_pages.push_back(std::move(page));
}

void JobWriter::end(JobEnd job_end) {
    const int sets_to_write = job_end == JobEnd::COMPLETE ? sets() : 1;
    for (; written_sets < sets_to_write; written_sets++) {
        std::uint32_t position_in_set = 0;
        for (const Page& page : repeated_pages) write_page(page, ++position_in_set);
    }
    RecordWriter document_end;
    document_end.begin(RecordType::END_DOCUMENT);
    write_bytes(document_end.view());
    write_trailer();
    flush();
}

void JobWriter::flush() const {
    if (std::fflush(output) != 0) throw std::system_error(errno, std::generic_category(), "unable to write the printer data");
}

void JobWriter::write_bytes(std::span<const std::uint8_t> bytes) const {
    if (std::fwrite(bytes.data(), 1, bytes.size(), output) != bytes.size())
        throw std::system_error(errno, std::generic_category(), "unable to write the printer data");
}

void JobWriter::write_header() const {
    std::tm    local{};
    const bool has_local_time = localtime_r(&job.time, &local) != nullptr;

    std::println(output, "{}@PJL JOB MODE=PRINTER", UNIVERSAL_EXIT_LANGUAGE);
    std::println(output, "@PJL SET STRINGCODESET=UTF8");
    if (has_local_time) {
        std::println(output, "@PJL COMMENT DATE={:02}/{:02}/{:04}", local.tm_mon + 1, local.tm_mday, local.tm_year + 1900);
        std::println(output, "@PJL COMMENT TIME={:02}:{:02}:{:02}", local.tm_hour, local.tm_min, local.tm_sec);
    }
    std::println(output, "@PJL COMMENT DNAME={}", job.title);
    std::println(output, "@PJL SET JOBATTR=\"@LUNA={}\"", job.user);
    if (settings.collate)
        std::println(output, "@PJL SET QTY=1");
    else
        std::println(output, "@PJL SET COPIES={}", settings.copies);
    std::println(output, "@PJL SET JOBATTR=\"@TRCH={}\"", on_off(settings.tray.auto_switch));
    if (!settings.tray.pjl_name.empty()) std::println(output, "@PJL SET JOBATTR=\"@INPT={}\"", settings.tray.pjl_name);
    if (settings.duplex == DuplexMode::NONE) {
        std::println(output, "@PJL SET DUPLEX=OFF");
    } else {
        std::println(output, "@PJL SET DUPLEX=ON");
        std::println(output, "@PJL SET BINDING={}", settings.duplex == DuplexMode::LONG_EDGE ? "LONGEDGE" : "SHORTEDGE");
    }
    std::println(output, "@PJL SET JOBATTR=\"@SPSE={}\"", settings.substitute_tray);
    std::println(output, "@PJL SET JOBATTR=\"@MSIP={}\"", settings.media);
    if (settings.media_back_face) std::println(output, "@PJL SET MEDIAFACE=BACKFACE");
    std::println(output, "@PJL SET RENDERMODE={}", settings.color_mode == ColorMode::GRAY ? "GRAYSCALE" : "COLOR");
    std::println(output, "@PJL SET ECONOMODE={}", on_off(settings.toner_saver));
    std::println(output, "@PJL SET JOBATTR=\"@IREC=OFF\"");
    std::println(output, "@PJL SET JOBATTR=\"@NLPP=1\"");
    if (!job.host.address.empty()) std::println(output, "@PJL SET JOBATTR=\"@HOAD={}\"", job.host.address);
    std::println(output, "@PJL SET JOBATTR=\"@JOAU={}\"", job.user);
    std::println(output, "@PJL SET JOBATTR=\"@CNAM={}\"", job.host.name);
    std::println(output, "@PJL SET OUTBIN=FACEDOWN");
    std::println(output, "@PJL SET JOBATTR=\"@PODR=NORMAL\"");
    std::println(output, "@PJL SET SLIPSHEET=OFF");
    std::println(output, "@PJL SET PAPERDIRECTION={}", settings.paper.long_edge_feed ? "LEF" : "SEF");
    std::println(output, "@PJL SET JOBATTR=\"@APSP={}\"", settings.preferred_media);
    std::println(output, "@PJL SET RESOLUTION={}", dots_per_inch(settings.resolution));
    std::println(output, "@PJL SET BITSPERPIXEL=8");
    std::println(output, "@PJL SET JOBATTR=\"@DRDM=RASTER\"");
    write_color_attributes();
    std::println(output, "@PJL SET JOBATTR=\"@BANR=DEVICE\"");
    if (settings.color_mode == ColorMode::COLOR && settings.correction_off) std::println(output, "@PJL SET JOBATTR=\"@FCMS=ON\"");
    std::println(output, "@PJL SET IWAMANUALDUP=OFF");
    std::println(output, "@PJL SET IWAJAMRECOVERY=AUTO");
    std::println(output, "@PJL ENTER LANGUAGE=HBPL");
}

void JobWriter::write_color_attributes() const {
    const ColorControls& color = settings.color;
    const auto           write_planes = [this](std::string_view attribute, int value) {
        for (std::string_view plane : PLANE_PREFIXES) std::println(output, "@PJL SET JOBATTR=\"@{}{}={}\"", plane, attribute, value);
    };

    if (settings.color_mode == ColorMode::GRAY) {
        write_planes("TRC", color.mono_tone_curve);
        write_planes("SCR", color.mono_screen);
        std::println(output, "@PJL SET JOBATTR=\"@PBLK=OFF\"");
        return;
    }
    write_planes("CPR", color.lookup_table);
    write_planes("UCR", color.under_color_removal);
    write_planes("TRC", color.tone_curve);
    write_planes("SCR", color.screen);
}

void JobWriter::write_page(const Page& page, std::uint32_t position_in_set) {
    const Paper&        paper = settings.paper;
    const auto          resolution = static_cast<float>(dots_per_inch(page.resolution));
    const bool          gray = page.color_mode == ColorMode::GRAY;
    const float         top_margin = paper.length - paper.top;
    const std::uint16_t custom_width = paper.custom ? to_pixels(paper.width, resolution) : 0;
    const std::uint16_t custom_length = paper.custom ? to_pixels(paper.length, resolution) : 0;
    const std::uint16_t left_margin_pixels = margin(paper.width, page.width, paper.left, resolution);
    const std::uint16_t top_margin_pixels = margin(paper.length, page.height, top_margin, resolution);
    const auto          data_length = BAND_HEADER_SIZE + static_cast<std::uint32_t>(page.compressed.size());
    std::uint8_t        duplex_side = NOT_DUPLEX;
    RecordWriter        record;

    if (settings.duplex != DuplexMode::NONE) duplex_side = position_in_set % 2 != 0 ? FRONT_SIDE : BACK_SIDE;
    page_number++;

    record.begin(RecordType::BEGIN_PAGE);
    record.byte_attribute(Attribute::LONG_EDGE_FEED, paper.long_edge_feed ? 1 : 0);
    record.byte_attribute(Attribute::TRAY, settings.tray.page_code);
    record.byte_attribute(Attribute::UNKNOWN_93, 1);
    record.byte_attribute(Attribute::PAPER_SIZE, paper.code);
    record.word_pair_attribute(Attribute::CUSTOM_PAPER_SIZE, custom_width, custom_length);
    record.byte_attribute(Attribute::UNKNOWN_96, 3);
    record.short_pair_attribute(Attribute::MARGINS, left_margin_pixels, top_margin_pixels);
    record.byte_attribute(Attribute::DUPLEX_SIDE, duplex_side);
    record.long_attribute(Attribute::PAGE_NUMBER, page_number);
    record.long_pair_attribute(Attribute::IMAGE_SIZE, page.width, page.height);
    record.byte_attribute(Attribute::UNKNOWN_9B, 0);
    record.byte_attribute(Attribute::UNKNOWN_9C, 1);
    record.byte_attribute(Attribute::COMPRESSION, gray ? MONO_COMPRESSION : COLOR_COMPRESSION);
    record.byte_attribute(Attribute::UNKNOWN_9E, 0);
    record.byte_attribute(Attribute::UNKNOWN_9F, 1);
    record.byte_attribute(Attribute::BITS_PER_PIXEL, gray ? MONO_BITS : COLOR_BITS);
    record.byte_attribute(Attribute::RESOLUTION, page.resolution == Resolution::DPI_300 ? RESOLUTION_300_DPI : RESOLUTION_600_DPI);
    record.long_pair_attribute(Attribute::IMAGE_SIZE_COPY, page.width, page.height);

    record.begin(RecordType::BEGIN_IMAGE);
    record.begin(RecordType::IMAGE_DATA);
    record.byte_attribute(Attribute::UNKNOWN_A3, 0);
    record.data_length_attribute(data_length);
    record.put_u32(BAND_HEADER_SIZE);
    record.put_u8(gray ? MONO_BITS : 0);
    record.put_u8(BAND_FORMAT);
    record.put_u16(0);
    record.put_u32(INITIAL_PREDICTOR_ORDER);
    record.put_u32(sq21::MAX_RUN_LENGTH);
    record.put_u32(BAND_UNKNOWN_FIELD);
    record.put_u32(0);
    write_bytes(record.view());
    write_bytes(page.compressed);

    RecordWriter page_end;
    page_end.begin(RecordType::END_IMAGE);
    page_end.begin(RecordType::END_PAGE);
    write_bytes(page_end.view());
}

void JobWriter::write_trailer() const {
    const Paper&      paper = settings.paper;
    const double      short_side_mm = to_millimeters(std::min(paper.width, paper.length));
    const double      long_side_mm = to_millimeters(std::max(paper.width, paper.length));
    const std::string size = std::format("{}{:.1f}X{:.1f}", paper.custom ? "*" : "", short_side_mm, long_side_mm);

    std::println(output, "{}@PJL COMMENT FXJOBINFO VERSION=1.0.0", UNIVERSAL_EXIT_LANGUAGE);
    std::println(output, "@PJL COMMENT FXJOBINFO BEGIN");
    std::println(output, "@PJL COMMENT FXJOBINFO PDLTYPE=PDL[PCLXL]:VERSION[1.0.0]");
    std::println(output, "@PJL COMMENT FXJOBINFO JOBTYPE=PRINT");
    std::println(output, "@PJL COMMENT FXJOBINFO JOBCOPIES=1");
    std::println(output, "@PJL COMMENT FXJOBINFO PAGECOPIES={}", settings.collate ? 1 : settings.copies);
    std::println(output, "@PJL COMMENT FXJOBINFO DUPLEXTYPE={}", duplex_type(settings.duplex));
    std::println(output, "@PJL COMMENT FXJOBINFO NUP=1");
    std::println(output, "@PJL COMMENT FXJOBINFO PAGEORDER=1TON");
    std::println(output, "@PJL COMMENT FXJOBINFO TONERSAVE={}", on_off(settings.toner_saver));
    std::println(output, "@PJL COMMENT FXJOBINFO PHYSICALPAGES={}", page_number);
    std::println(output, "@PJL COMMENT FXJOBINFO PAGEINFOBEGIN");
    for (int set = 0; set < written_sets; set++) {
        for (const ColorMode color_mode : page_color_modes) {
            const std::string_view mode = color_mode == ColorMode::GRAY ? "MODE[MONOCHROME]" : "MODE[COLOR]:COLORS[0]";
            std::println(output, "@PJL COMMENT FXJOBINFO PAGEINFO=SIZE[{}]:{}:INTRAY[{}]", size, mode, settings.tray.job_info_code);
        }
    }
    std::println(output, "@PJL COMMENT FXJOBINFO PAGEINFOEND");
    std::println(output, "@PJL COMMENT FXJOBINFO END");
    std::println(output, "@PJL EOJ");
}

}
