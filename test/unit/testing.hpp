#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <print>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <unistd.h>

namespace dell1320c::testing {

inline int g_failure_count = 0;

struct TestCase {
    std::string_view name;
    void (*run)();
};

inline void report_failure(const std::source_location& location, std::string_view message) {
    g_failure_count++;
    std::println(stderr, "{}:{}: {}", location.file_name(), location.line(), message);
}

inline void check(bool condition, std::string_view description, std::source_location location = std::source_location::current()) {
    if (!condition) report_failure(location, std::format("check failed: {}", description));
}

template <typename Actual, typename Expected>
void check_equal(const Actual& actual, const Expected& expected, std::source_location location = std::source_location::current()) {
    if (actual == expected) return;
    if constexpr (std::formattable<Actual, char> && std::formattable<Expected, char>)
        report_failure(location, std::format("expected {} but got {}", expected, actual));
    else
        report_failure(location, "the values differ");
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::string pattern = (std::filesystem::temp_directory_path() / "dell1320c-unit.XXXXXX").string();
        if (!mkdtemp(pattern.data())) throw std::system_error(errno, std::generic_category(), "unable to create a temporary directory");
        directory = pattern;
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

    const std::filesystem::path& path() const noexcept {
        return directory;
    }

private:
    std::filesystem::path directory;
};

inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(std::format("unable to open {}", path.string()));
    std::string contents(std::filesystem::file_size(path), '\0');
    if (!input.read(contents.data(), static_cast<std::streamsize>(contents.size())))
        throw std::runtime_error(std::format("unable to read {}", path.string()));
    return contents;
}

inline std::string hex(std::span<const std::uint8_t> bytes) {
    std::string text;
    for (std::uint8_t byte : bytes) std::format_to(std::back_inserter(text), "{:02x}", byte);
    return text;
}

template <typename Exception, typename Callable>
void check_throws(const Callable& callable, std::source_location location = std::source_location::current()) {
    try {
        callable();
    } catch (const Exception&) {
        return;
    } catch (const std::exception& error) {
        report_failure(location, std::format("threw a different exception: {}", error.what()));
        return;
    }
    report_failure(location, "did not throw");
}

std::span<const TestCase> settings_tests();
std::span<const TestCase> color_table_tests();
std::span<const TestCase> sq21_tests();
std::span<const TestCase> hbpl_tests();
std::span<const TestCase> page_converter_tests();
std::span<const TestCase> raster_reader_tests();

}
