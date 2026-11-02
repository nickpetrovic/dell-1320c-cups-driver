#include <atomic>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <expected>
#include <filesystem>
#include <new>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <signal.h>

#include "color_table.hpp"
#include "hbpl.hpp"
#include "page_converter.hpp"
#include "raster_reader.hpp"
#include "settings.hpp"

#ifndef DELL1320C_COLOR_DIR
#define DELL1320C_COLOR_DIR "/usr/share/dell1320c/color"
#endif

namespace {

using dell1320c::ColorTable;
using dell1320c::Settings;

constexpr std::string_view DEFAULT_COLOR_DIRECTORY = DELL1320C_COLOR_DIR;
constexpr std::string_view PROGRAM_NAME = "rastertodell1320c";

std::atomic<bool> g_cancelled = false;
static_assert(std::atomic<bool>::is_always_lock_free);

void on_terminate(int) {
    g_cancelled.store(true, std::memory_order_relaxed);
}

void install_signal_handlers() {
    struct sigaction ignore{};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    sigaction(SIGPIPE, &ignore, nullptr);

    struct sigaction terminate{};
    terminate.sa_handler = on_terminate;
    terminate.sa_flags = SA_RESTART;
    sigemptyset(&terminate.sa_mask);
    sigaction(SIGTERM, &terminate, nullptr);
}

int fail(std::string_view message) noexcept {
    std::fputs("ERROR: ", stderr);
    std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
    return 1;
}

std::string_view environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string_view(value) : std::string_view();
}

int parse_copies(std::string_view text) {
    int copies = 1;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), copies);
    const bool whole_number = error == std::errc() && end == text.data() + text.size();
    return whole_number ? copies : 1;
}

std::expected<std::optional<ColorTable>, std::string> load_color_table(const Settings& settings) {
    std::optional<dell1320c::ColorTableChoice> choice = dell1320c::color_table_for(settings);
    if (!choice) return std::nullopt;

    std::string_view directory = environment("DELL1320C_COLOR_DIR");
    if (directory.empty()) directory = DEFAULT_COLOR_DIRECTORY;
    return ColorTable::load(directory, choice->name, choice->output);
}

dell1320c::hbpl::Job make_job(std::string_view user, std::string_view title) {
    return {
        .user = dell1320c::hbpl::printable_text(user),
        .title = dell1320c::hbpl::printable_text(title),
        .host = dell1320c::hbpl::host_identity(),
        .time = std::time(nullptr),
    };
}

using JobResult = std::expected<void, std::string>;

JobResult convert_pages(
    dell1320c::RasterReader& reader, dell1320c::PageConverter& converter, dell1320c::hbpl::JobWriter& writer, int copies
) {
    unsigned page_number = 0;

    while (!g_cancelled.load(std::memory_order_relaxed)) {
        dell1320c::PageHeaderResult next_page = reader.next_page();
        if (!next_page) return std::unexpected(std::move(next_page.error()));
        if (!*next_page) break;

        const cups_page_header2_t& header = **next_page;
        dell1320c::PageResult      converted = converter.convert(reader, header, page_number + 1);
        if (!converted) return std::unexpected(std::move(converted.error()));

        std::optional<dell1320c::hbpl::Page>& page = *converted;
        if (!page) continue;

        page_number++;
        writer.add_page(std::move(*page));
        std::println(stderr, "PAGE: {} {}", page_number, copies);
    }
    return {};
}

JobResult convert_job(
    dell1320c::RasterReader& reader, dell1320c::PageConverter& converter, dell1320c::hbpl::JobWriter& writer, int copies
) {
    try {
        return convert_pages(reader, converter, writer, copies);
    } catch (const std::bad_alloc&) {
        return std::unexpected(std::string("out of memory"));
    } catch (const std::exception& error) {
        return std::unexpected(std::string(error.what()));
    }
}

int run(std::span<char*> arguments) {
    if (arguments.size() != 6 && arguments.size() != 7) {
        std::string_view program = arguments.empty() ? PROGRAM_NAME : std::string_view(arguments[0]);
        std::println(stderr, "Usage: {} job-id user title copies options [file]", program);
        return 1;
    }
    install_signal_handlers();
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    const std::string_view ppd_path = environment("PPD");
    if (ppd_path.empty()) return fail("the PPD environment variable is not set");

    const auto settings = dell1320c::load_settings(std::string(ppd_path), arguments[5], parse_copies(arguments[4]));
    if (!settings) return fail(settings.error());

    auto loaded_table = load_color_table(*settings);
    if (!loaded_table) return fail(loaded_table.error());

    auto reader = dell1320c::RasterReader::open(arguments.size() == 7 ? arguments[6] : nullptr);
    if (!reader) return fail(reader.error());

    std::optional<ColorTable>& color_table = *loaded_table;
    const ColorTable*          table = color_table ? &*color_table : nullptr;
    dell1320c::PageConverter   converter(*settings, table, environment("DELL1320C_DUMP_DIR"));
    dell1320c::hbpl::JobWriter writer(stdout, *settings, make_job(arguments[2], arguments[3]));

    writer.begin();
    const JobResult converted = convert_job(*reader, converter, writer, settings->copies);
    const bool      complete = converted.has_value() && !g_cancelled.load(std::memory_order_relaxed);
    writer.end(complete ? dell1320c::hbpl::JobEnd::COMPLETE : dell1320c::hbpl::JobEnd::INTERRUPTED);
    if (!converted) return fail(converted.error());
    return 0;
}

}

int main(int argc, char* argv[]) {
    try {
        return run(std::span(argv, static_cast<std::size_t>(argc)));
    } catch (const std::bad_alloc&) {
        return fail("out of memory");
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}
