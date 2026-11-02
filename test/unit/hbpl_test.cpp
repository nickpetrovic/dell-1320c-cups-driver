#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "hbpl.hpp"
#include "testing.hpp"
#include "unique_c_pointer.hpp"

namespace dell1320c::testing {
namespace {

constexpr std::string_view HEADER_END = "@PJL ENTER LANGUAGE=HBPL\n";
constexpr std::size_t      DOCUMENT_BEGIN_SIZE = 12;
constexpr std::size_t      PAGE_RECORD_SIZE = 114;
constexpr std::size_t      BAND_HEADER_SIZE = 24;
constexpr char             BEGIN_PAGE = 0x43;
constexpr std::size_t      LONG_EDGE_FEED_OFFSET = 3;
constexpr std::size_t      PAPER_SIZE_OFFSET = 12;
constexpr std::size_t      CUSTOM_WIDTH_OFFSET = 15;
constexpr std::size_t      CUSTOM_LENGTH_OFFSET = 17;
constexpr std::size_t      LEFT_MARGIN_OFFSET = 24;
constexpr std::size_t      TOP_MARGIN_OFFSET = 26;
constexpr std::size_t      DUPLEX_SIDE_OFFSET = 30;
constexpr std::size_t      PAGE_NUMBER_OFFSET = 33;
constexpr std::size_t      COMPRESSION_OFFSET = 55;
constexpr std::size_t      BITS_PER_PIXEL_OFFSET = 64;
constexpr std::size_t      RESOLUTION_OFFSET = 67;
constexpr std::size_t      DATA_LENGTH_OFFSET = 86;

using File = UniqueCPointer<std::FILE, [](std::FILE* file) { std::fclose(file); }>;

struct ParsedPage {
    std::uint8_t              long_edge_feed;
    std::uint8_t              paper_size;
    std::uint16_t             custom_width;
    std::uint16_t             custom_length;
    std::uint16_t             left_margin;
    std::uint16_t             top_margin;
    std::uint8_t              duplex_side;
    std::uint32_t             page_number;
    std::uint8_t              compression;
    std::uint8_t              bits_per_pixel;
    std::uint8_t              resolution;
    std::vector<std::uint8_t> compressed;
};

struct ParsedJob {
    std::string             header = {};
    std::vector<ParsedPage> pages = {};
    std::string             trailer = {};
};

std::uint8_t read_u8(std::string_view bytes, std::size_t offset) {
    return static_cast<std::uint8_t>(bytes.at(offset));
}

std::uint16_t read_u16(std::string_view bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(read_u8(bytes, offset + 1) << 8 | read_u8(bytes, offset));
}

std::uint32_t read_u32(std::string_view bytes, std::size_t offset) {
    return std::uint32_t{read_u16(bytes, offset + 2)} << 16 | read_u16(bytes, offset);
}

ParsedPage parse_page(std::string_view record, std::string_view data) {
    return {
        .long_edge_feed = read_u8(record, LONG_EDGE_FEED_OFFSET),
        .paper_size = read_u8(record, PAPER_SIZE_OFFSET),
        .custom_width = read_u16(record, CUSTOM_WIDTH_OFFSET),
        .custom_length = read_u16(record, CUSTOM_LENGTH_OFFSET),
        .left_margin = read_u16(record, LEFT_MARGIN_OFFSET),
        .top_margin = read_u16(record, TOP_MARGIN_OFFSET),
        .duplex_side = read_u8(record, DUPLEX_SIDE_OFFSET),
        .page_number = read_u32(record, PAGE_NUMBER_OFFSET),
        .compression = read_u8(record, COMPRESSION_OFFSET),
        .bits_per_pixel = read_u8(record, BITS_PER_PIXEL_OFFSET),
        .resolution = read_u8(record, RESOLUTION_OFFSET),
        .compressed = std::vector<std::uint8_t>(data.begin(), data.end()),
    };
}

ParsedJob parse_job(std::string_view output) {
    std::size_t header_size = output.find(HEADER_END);
    if (header_size == std::string_view::npos) throw std::runtime_error("the job has no HBPL header");
    header_size += HEADER_END.size();

    ParsedJob   job{.header = std::string(output.substr(0, header_size))};
    std::size_t position = header_size + DOCUMENT_BEGIN_SIZE;
    while (position < output.size() && output[position] == BEGIN_PAGE) {
        const std::string_view record = output.substr(position, PAGE_RECORD_SIZE);
        const std::size_t      data_size = read_u32(record, DATA_LENGTH_OFFSET) - BAND_HEADER_SIZE;
        const std::size_t      data_start = position + PAGE_RECORD_SIZE;
        job.pages.push_back(parse_page(record, output.substr(data_start, data_size)));
        if (output.substr(data_start + data_size, 2) != "SD") throw std::runtime_error("a page does not end with SD");
        position = data_start + data_size + 2;
    }
    if (output.substr(position, 1) != "B") throw std::runtime_error("the document does not end with B");
    job.trailer = std::string(output.substr(position + 1));
    return job;
}

class MemoryStream {
public:
    MemoryStream() : file(open_memstream(&buffer, &size)) {
        if (!file) throw std::system_error(errno, std::generic_category(), "unable to open a memory stream");
    }

    MemoryStream(const MemoryStream&) = delete;
    MemoryStream& operator=(const MemoryStream&) = delete;
    MemoryStream(MemoryStream&&) = delete;
    MemoryStream& operator=(MemoryStream&&) = delete;

    ~MemoryStream() {
        std::fclose(file);
        std::free(buffer);
    }

    [[nodiscard]] std::FILE* stream() const noexcept {
        return file;
    }

    [[nodiscard]] std::string_view contents() {
        if (std::fflush(file) != 0) throw std::system_error(errno, std::generic_category(), "unable to flush the memory stream");
        return {buffer, size};
    }

private:
    char*       buffer = nullptr;
    std::size_t size = 0;
    std::FILE*  file;
};

Settings letter_settings() {
    Settings settings;
    settings.paper = {.width = 612, .length = 792, .left = 11.28f, .bottom = 11.28f, .right = 600.72f, .top = 780.72f, .code = 0x00};
    settings.tray = {.page_code = 2, .job_info_code = 1, .pjl_name = "INTRAY1"};
    settings.color = {
        .lookup_table = 24, .under_color_removal = 24, .tone_curve = 9, .screen = 9, .mono_tone_curve = 59, .mono_screen = 59
    };
    settings.document_type = "Normal";
    settings.media = "NORMAL";
    settings.preferred_media = "OFF";
    settings.substitute_tray = "AUTO";
    return settings;
}

hbpl::Job test_job() {
    return {
        .user = "alice",
        .title = "report",
        .host = {.name = "desk", .address = "IC0A80001"},
        .time = 0,
    };
}

hbpl::Page page_with(std::uint8_t marker, std::uint32_t width = 4912, std::uint32_t height = 6416) {
    return {
        .width = width, .height = height, .compressed = {marker, 0xff}
    };
}

ParsedJob write_job(const Settings& settings, std::vector<hbpl::Page> pages) {
    MemoryStream    memory;
    hbpl::JobWriter writer(memory.stream(), settings, test_job());

    writer.begin();
    for (hbpl::Page& page : pages) writer.add_page(std::move(page));
    writer.end(hbpl::JobEnd::COMPLETE);
    return parse_job(memory.contents());
}

void the_header_describes_the_job() {
    Settings settings = letter_settings();
    settings.copies = 2;
    const ParsedJob job = write_job(settings, {page_with(1)});

    check(job.header.starts_with("\033%-12345X@PJL JOB MODE=PRINTER\n"), "the job starts with the universal exit language");
    check(job.header.contains("@PJL COMMENT DNAME=report\n"), "the title is in the header");
    check(job.header.contains("@PJL SET JOBATTR=\"@LUNA=alice\"\n"), "the user is in the header");
    check(job.header.contains("@PJL SET COPIES=2\n"), "uncollated copies are counted by the printer");
    check(job.header.contains("@PJL SET JOBATTR=\"@INPT=INTRAY1\"\n"), "the tray is in the header");
    check(job.header.contains("@PJL SET JOBATTR=\"@HOAD=IC0A80001\"\n"), "the host address is in the header");
    check(job.header.contains("@PJL SET JOBATTR=\"@TCPR=24\"\n"), "the color controls are in the header");
    check(job.header.contains("@PJL SET RESOLUTION=600\n"), "the resolution is in the header");
    check(!job.header.contains("@FCMS"), "color management stays on");
    check(job.trailer.contains("PAGECOPIES=2\n"), "the trailer counts the uncollated copies");
    check(job.trailer.contains("PAGEINFO=SIZE[215.9X279.4]:MODE[COLOR]:COLORS[0]:INTRAY[1]\n"), "the trailer describes the page");
    check(job.trailer.ends_with("@PJL EOJ\n"), "the job ends with EOJ");
}

void collated_copies_repeat_the_pages() {
    Settings settings = letter_settings();
    settings.copies = 2;
    settings.collate = true;
    settings.duplex = DuplexMode::LONG_EDGE;
    ParsedJob job = write_job(settings, {page_with(1), page_with(2), page_with(3)});

    check(job.header.contains("@PJL SET QTY=1\n"), "collated copies are sent one set at a time");
    check(job.trailer.contains("PAGECOPIES=1\n"), "the printer makes no copies itself");
    check_equal(job.pages.size(), std::size_t{6});
    if (job.pages.size() != 6) return;

    constexpr std::array<std::uint8_t, 6> SIDES = {1, 2, 1, 1, 2, 1};
    for (std::size_t i = 0; i < job.pages.size(); i++) {
        check_equal(job.pages[i].page_number, static_cast<std::uint32_t>(i + 1));
        check_equal(job.pages[i].duplex_side, SIDES[i]);
        check_equal(job.pages[i].compressed.front(), static_cast<std::uint8_t>(i % 3 + 1));
    }
    check(job.trailer.contains("PHYSICALPAGES=6\n"), "the trailer counts every printed page");
    check(job.trailer.contains("DUPLEXTYPE=DUPLEX\n"), "long edge duplex");
}

void margins_are_kept_only_when_the_image_fits() {
    const ParsedPage fits = write_job(letter_settings(), {page_with(1)}).pages.at(0);
    const ParsedPage exactly_fits = write_job(letter_settings(), {page_with(1, 5006, 6506)}).pages.at(0);
    const ParsedPage too_large = write_job(letter_settings(), {page_with(1, 5104, 6600)}).pages.at(0);

    check_equal(fits.left_margin, std::uint16_t{94});
    check_equal(fits.top_margin, std::uint16_t{94});
    check_equal(exactly_fits.left_margin, std::uint16_t{94});
    check_equal(exactly_fits.top_margin, std::uint16_t{94});
    check_equal(too_large.left_margin, std::uint16_t{0});
    check_equal(too_large.top_margin, std::uint16_t{0});
}

void page_attributes_follow_the_page() {
    Settings landscape = letter_settings();
    landscape.paper.long_edge_feed = true;
    hbpl::Page draft = page_with(1);
    draft.resolution = Resolution::DPI_300;

    const ParsedPage color = write_job(letter_settings(), {page_with(1)}).pages.at(0);
    const ParsedPage turned = write_job(landscape, {page_with(1)}).pages.at(0);
    const ParsedPage low_resolution = write_job(letter_settings(), {draft}).pages.at(0);

    check_equal(color.long_edge_feed, std::uint8_t{0});
    check_equal(color.paper_size, std::uint8_t{0});
    check_equal(color.compression, std::uint8_t{0x16});
    check_equal(color.bits_per_pixel, std::uint8_t{0x20});
    check_equal(color.resolution, std::uint8_t{0});
    check_equal(turned.long_edge_feed, std::uint8_t{1});
    check_equal(low_resolution.resolution, std::uint8_t{3});
}

void gray_jobs_use_the_gray_settings() {
    Settings settings = letter_settings();
    settings.color_mode = ColorMode::GRAY;
    settings.correction_off = true;
    hbpl::Page page = page_with(1);
    page.color_mode = ColorMode::GRAY;
    ParsedJob job = write_job(settings, {page});

    check(job.header.contains("@PJL SET RENDERMODE=GRAYSCALE\n"), "the job is rendered in gray");
    check(job.header.contains("@PJL SET JOBATTR=\"@TTRC=59\"\n"), "the gray tone curve is in the header");
    check(job.header.contains("@PJL SET JOBATTR=\"@PBLK=OFF\"\n"), "gray jobs turn pure black off");
    check(!job.header.contains("@FCMS"), "gray jobs never turn color management off");
    check_equal(job.pages.at(0).compression, std::uint8_t{0x11});
    check_equal(job.pages.at(0).bits_per_pixel, std::uint8_t{0x08});
    check(job.trailer.contains(":MODE[MONOCHROME]:INTRAY[1]\n"), "the trailer describes a gray page");
}

void custom_paper_is_written_in_pixels() {
    Settings settings = letter_settings();
    settings.paper = {.width = 612, .length = 1008, .right = 612, .top = 1008, .code = 0xff, .custom = true};
    ParsedJob job = write_job(settings, {page_with(1, 5104, 8400)});

    check_equal(job.pages.at(0).paper_size, std::uint8_t{0xff});
    check_equal(job.pages.at(0).custom_width, std::uint16_t{5100});
    check_equal(job.pages.at(0).custom_length, std::uint16_t{8400});
    check(job.trailer.contains("PAGEINFO=SIZE[*215.9X355.6]:"), "the trailer marks the custom size");
}

void color_off_and_back_face_lines() {
    Settings settings = letter_settings();
    settings.correction_off = true;
    settings.media_back_face = true;
    const ParsedJob job = write_job(settings, {});

    check(job.header.contains("@PJL SET JOBATTR=\"@MSIP=NORMAL\"\n@PJL SET MEDIAFACE=BACKFACE\n"), "the back face follows the media");
    check(job.header.contains("@PJL SET JOBATTR=\"@BANR=DEVICE\"\n@PJL SET JOBATTR=\"@FCMS=ON\"\n"), "color management is off");
    check(job.pages.empty(), "a job can have no pages");
    check(job.trailer.contains("PHYSICALPAGES=0\n"), "the trailer counts no pages");
    check(!job.trailer.contains("PAGEINFO=SIZE"), "no page descriptions");
}

void text_and_host_identity() {
    check_equal(hbpl::printable_text("tab\there\nnew\x7fline"), std::string("tab here new line"));
    check_equal(hbpl::printable_text("say \"hi\""), std::string("say  hi "));

    check_equal(hbpl::host_address_text({192, 168, 56, 15}), std::string("IC0A838F"));
    check_equal(hbpl::host_address_text({10, 0, 0, 1}), std::string("IA001"));

    const hbpl::HostIdentity identity = hbpl::host_identity();
    check(!identity.name.empty(), "the host has a name");
    if (!identity.address.empty()) {
        check(identity.address.starts_with('I'), "the address starts with I");
        check(identity.address.find_first_not_of("I0123456789ABCDEF") == std::string::npos, "the address is uppercase hexadecimal");
    }
}

void an_interrupted_job_does_not_repeat_its_copies() {
    Settings settings = letter_settings();
    settings.copies = 3;
    settings.collate = true;
    MemoryStream    memory;
    hbpl::JobWriter writer(memory.stream(), settings, test_job());

    writer.begin();
    writer.add_page(page_with(1));
    writer.end(hbpl::JobEnd::INTERRUPTED);
    const ParsedJob job = parse_job(memory.contents());

    check_equal(job.pages.size(), std::size_t{1});
    check(job.trailer.contains("PHYSICALPAGES=1\n"), "the trailer counts only the page that was printed");
    check(job.trailer.ends_with("@PJL EOJ\n"), "the job still ends with EOJ");
}

void a_failed_write_is_an_error() {
    const Settings settings = letter_settings();
    const File     read_only(std::fopen("/dev/null", "r"));
    if (!read_only) throw std::runtime_error("unable to open /dev/null");

    hbpl::JobWriter writer(read_only.get(), settings, test_job());
    check_throws<std::system_error>([&] { writer.begin(); });
}

constexpr std::array TESTS = {
    TestCase{"hbpl: the header describes the job",                  the_header_describes_the_job                 },
    TestCase{"hbpl: collated copies repeat the pages",              collated_copies_repeat_the_pages             },
    TestCase{"hbpl: margins are kept only when the image fits",     margins_are_kept_only_when_the_image_fits    },
    TestCase{"hbpl: page attributes follow the page",               page_attributes_follow_the_page              },
    TestCase{"hbpl: gray jobs use the gray settings",               gray_jobs_use_the_gray_settings              },
    TestCase{"hbpl: custom paper is written in pixels",             custom_paper_is_written_in_pixels            },
    TestCase{"hbpl: color off, back face and an empty job",         color_off_and_back_face_lines                },
    TestCase{"hbpl: printable text and the host identity",          text_and_host_identity                       },
    TestCase{"hbpl: a failed write is an error",                    a_failed_write_is_an_error                   },
    TestCase{"hbpl: an interrupted job does not repeat its copies", an_interrupted_job_does_not_repeat_its_copies},
};

}

std::span<const TestCase> hbpl_tests() {
    return TESTS;
}

}
