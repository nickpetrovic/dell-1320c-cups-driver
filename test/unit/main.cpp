#include <array>
#include <cstdio>
#include <exception>
#include <print>

#include "testing.hpp"

using dell1320c::testing::TestCase;

namespace {

int run_tests() {
    constexpr std::array SUITES = {
        &dell1320c::testing::settings_tests, &dell1320c::testing::color_table_tests,    &dell1320c::testing::sq21_tests,
        &dell1320c::testing::hbpl_tests,     &dell1320c::testing::page_converter_tests, &dell1320c::testing::raster_reader_tests,
    };
    int test_count = 0;

    for (auto suite : SUITES) {
        for (const TestCase& test : suite()) {
            const int failures_before = dell1320c::testing::g_failure_count;
            test_count++;
            try {
                test.run();
            } catch (const std::exception& error) {
                dell1320c::testing::g_failure_count++;
                std::println(stderr, "unexpected exception: {}", error.what());
            }
            const bool passed = dell1320c::testing::g_failure_count == failures_before;
            std::println("{} {}", passed ? "PASS " : "FAIL ", test.name);
        }
    }

    std::println("\n{} tests, {} failed checks", test_count, dell1320c::testing::g_failure_count);
    return dell1320c::testing::g_failure_count == 0 ? 0 : 1;
}

}

int main() {
    try {
        return run_tests();
    } catch (const std::exception& error) {
        std::fputs(error.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
