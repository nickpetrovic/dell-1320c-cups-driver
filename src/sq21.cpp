#include "sq21.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace dell1320c::sq21 {
namespace {

constexpr int           LINE_MARGIN = 128;
constexpr int           LINE_PADDING = 140;
constexpr int           PREDICTOR_COUNT = 5;
constexpr int           NEAR_MATCH_RUN_LIMIT = 64;
constexpr int           LONGEST_RUN = static_cast<int>(MAX_RUN_LENGTH);
constexpr int           SHORT_RUN_LIMIT = 26;
constexpr int           MARKER_REFRESH_DISTANCE = 32;
constexpr int           TABLE_SWITCH_THRESHOLD = 8;
constexpr std::size_t   LITERAL_SYMBOL = 5;
constexpr std::size_t   ESCAPE_SYMBOL = 6;
constexpr std::uint32_t LONG_RUN_GROUP_BASE = 0x31;
constexpr unsigned      LONG_RUN_GROUP_BITS = 6;
constexpr std::size_t   INITIAL_OUTPUT_CAPACITY = std::size_t{64} * 1024;

struct Code {
    std::uint32_t value;
    unsigned      bits;
};

constexpr Code TABLE_SWITCH = {0x0, 1};
constexpr Code END_OF_PAGE = {0x7, 3};

constexpr std::array<Code, 256> LITERAL_DELTA_CODES = {
    {{0x000, 3},  {0x001, 3},  {0x00c, 5},  {0x00d, 5},  {0x020, 6},  {0x021, 6},  {0x022, 6},  {0x023, 6},  {0x050, 7},  {0x051, 7},
     {0x052, 7},  {0x053, 7},  {0x054, 7},  {0x055, 7},  {0x056, 7},  {0x057, 7},  {0x180, 9},  {0x181, 9},  {0x182, 9},  {0x183, 9},
     {0x184, 9},  {0x185, 9},  {0x186, 9},  {0x187, 9},  {0x188, 9},  {0x189, 9},  {0x18a, 9},  {0x18b, 9},  {0x18c, 9},  {0x18d, 9},
     {0x18e, 9},  {0x18f, 9},  {0x190, 9},  {0x191, 9},  {0x192, 9},  {0x193, 9},  {0x194, 9},  {0x195, 9},  {0x196, 9},  {0x197, 9},
     {0x198, 9},  {0x199, 9},  {0x19a, 9},  {0x19b, 9},  {0x19c, 9},  {0x19d, 9},  {0x19e, 9},  {0x19f, 9},  {0x380, 10}, {0x381, 10},
     {0x382, 10}, {0x383, 10}, {0x384, 10}, {0x385, 10}, {0x386, 10}, {0x387, 10}, {0x388, 10}, {0x389, 10}, {0x38a, 10}, {0x38b, 10},
     {0x38c, 10}, {0x38d, 10}, {0x38e, 10}, {0x38f, 10}, {0x390, 10}, {0x391, 10}, {0x392, 10}, {0x393, 10}, {0x394, 10}, {0x395, 10},
     {0x396, 10}, {0x397, 10}, {0x398, 10}, {0x399, 10}, {0x39a, 10}, {0x39b, 10}, {0x39c, 10}, {0x39d, 10}, {0x39e, 10}, {0x39f, 10},
     {0x780, 11}, {0x781, 11}, {0x782, 11}, {0x783, 11}, {0x784, 11}, {0x785, 11}, {0x786, 11}, {0x787, 11}, {0x788, 11}, {0x789, 11},
     {0x78a, 11}, {0x78b, 11}, {0x78c, 11}, {0x78d, 11}, {0x78e, 11}, {0x78f, 11}, {0x790, 11}, {0x791, 11}, {0x792, 11}, {0x793, 11},
     {0x794, 11}, {0x795, 11}, {0x796, 11}, {0x797, 11}, {0x798, 11}, {0x799, 11}, {0x79a, 11}, {0x79b, 11}, {0x79c, 11}, {0x79d, 11},
     {0x79e, 11}, {0x79f, 11}, {0x7a0, 11}, {0x7a1, 11}, {0x7a2, 11}, {0x7a3, 11}, {0x7a4, 11}, {0x7a5, 11}, {0x7a6, 11}, {0x7a7, 11},
     {0x7a8, 11}, {0x7a9, 11}, {0x7aa, 11}, {0x7ab, 11}, {0x7ac, 11}, {0x7ad, 11}, {0x7ae, 11}, {0x7af, 11}, {0x7f0, 11}, {0x7ef, 11},
     {0x7ee, 11}, {0x7ed, 11}, {0x7ec, 11}, {0x7eb, 11}, {0x7ea, 11}, {0x7e9, 11}, {0x7e8, 11}, {0x7e7, 11}, {0x7e6, 11}, {0x7e5, 11},
     {0x7e4, 11}, {0x7e3, 11}, {0x7e2, 11}, {0x7e1, 11}, {0x7e0, 11}, {0x7df, 11}, {0x7de, 11}, {0x7dd, 11}, {0x7dc, 11}, {0x7db, 11},
     {0x7da, 11}, {0x7d9, 11}, {0x7d8, 11}, {0x7d7, 11}, {0x7d6, 11}, {0x7d5, 11}, {0x7d4, 11}, {0x7d3, 11}, {0x7d2, 11}, {0x7d1, 11},
     {0x7d0, 11}, {0x7cf, 11}, {0x7ce, 11}, {0x7cd, 11}, {0x7cc, 11}, {0x7cb, 11}, {0x7ca, 11}, {0x7c9, 11}, {0x7c8, 11}, {0x7c7, 11},
     {0x7c6, 11}, {0x7c5, 11}, {0x7c4, 11}, {0x7c3, 11}, {0x7c2, 11}, {0x7c1, 11}, {0x7c0, 11}, {0x3bf, 10}, {0x3be, 10}, {0x3bd, 10},
     {0x3bc, 10}, {0x3bb, 10}, {0x3ba, 10}, {0x3b9, 10}, {0x3b8, 10}, {0x3b7, 10}, {0x3b6, 10}, {0x3b5, 10}, {0x3b4, 10}, {0x3b3, 10},
     {0x3b2, 10}, {0x3b1, 10}, {0x3b0, 10}, {0x3af, 10}, {0x3ae, 10}, {0x3ad, 10}, {0x3ac, 10}, {0x3ab, 10}, {0x3aa, 10}, {0x3a9, 10},
     {0x3a8, 10}, {0x3a7, 10}, {0x3a6, 10}, {0x3a5, 10}, {0x3a4, 10}, {0x3a3, 10}, {0x3a2, 10}, {0x3a1, 10}, {0x3a0, 10}, {0x1bf, 9},
     {0x1be, 9},  {0x1bd, 9},  {0x1bc, 9},  {0x1bb, 9},  {0x1ba, 9},  {0x1b9, 9},  {0x1b8, 9},  {0x1b7, 9},  {0x1b6, 9},  {0x1b5, 9},
     {0x1b4, 9},  {0x1b3, 9},  {0x1b2, 9},  {0x1b1, 9},  {0x1b0, 9},  {0x1af, 9},  {0x1ae, 9},  {0x1ad, 9},  {0x1ac, 9},  {0x1ab, 9},
     {0x1aa, 9},  {0x1a9, 9},  {0x1a8, 9},  {0x1a7, 9},  {0x1a6, 9},  {0x1a5, 9},  {0x1a4, 9},  {0x1a3, 9},  {0x1a2, 9},  {0x1a1, 9},
     {0x1a0, 9},  {0x05f, 7},  {0x05e, 7},  {0x05d, 7},  {0x05c, 7},  {0x05b, 7},  {0x05a, 7},  {0x059, 7},  {0x058, 7},  {0x027, 6},
     {0x026, 6},  {0x025, 6},  {0x024, 6},  {0x00f, 5},  {0x00e, 5},  {0x002, 3}}
};

constexpr std::array<std::array<Code, 7>, 2> SYMBOL_CODES = {
    {
     {{{0x0, 1}, {0x6, 3}, {0x1c, 5}, {0x1d, 5}, {0x1e, 5}, {0x2, 2}, {0x1f, 5}}},
     {{{0x2, 2}, {0x6, 3}, {0x1c, 5}, {0x1d, 5}, {0x1e, 5}, {0x0, 1}, {0x1f, 5}}},
     }
};

constexpr std::array<Code, SHORT_RUN_LIMIT> SHORT_RUN_CODES = {
    {
     {0x00, 0}, {0x00, 2}, {0x02, 3}, {0x03, 3}, {0x08, 4}, {0x09, 4}, {0x14, 5}, {0x15, 5}, {0x16, 5},
     {0x17, 5}, {0x60, 7}, {0x61, 7}, {0x62, 7}, {0x63, 7}, {0x64, 7}, {0x65, 7}, {0x66, 7}, {0x67, 7},
     {0x68, 7}, {0x69, 7}, {0x6a, 7}, {0x6b, 7}, {0x6c, 7}, {0x6d, 7}, {0x6e, 7}, {0x6f, 7},
     }
};

constexpr std::array<int, 16> LONG_RUN_GROUP_STARTS = {0, 1, 2, 3, 4, 6, 10, 26, 34, 42, 50, 66, 98, 162, 674, 17058};

constexpr std::array<unsigned, 15> LONG_RUN_EXTRA_BITS = {0, 0, 0, 0, 1, 2, 4, 3, 3, 3, 4, 5, 6, 9, 14};

static_assert(LONG_RUN_GROUP_STARTS.back() > LONGEST_RUN);
static_assert(LONG_RUN_GROUP_STARTS[7] == SHORT_RUN_LIMIT);

class BitWriter {
public:
    BitWriter() {
        bytes.reserve(INITIAL_OUTPUT_CAPACITY);
    }

    void put(Code code) {
        put(code.value, code.bits);
    }

    void put(std::uint64_t value, unsigned bits) {
        accumulator = accumulator << bits | value;
        pending_bits += bits;
        while (pending_bits >= 8) {
            pending_bits -= 8;
            bytes.push_back(static_cast<std::uint8_t>(accumulator >> pending_bits));
        }
    }

    void pad_with_ones() {
        if (pending_bits == 0) return;
        const unsigned padding_bits = 8 - pending_bits;
        put((1u << padding_bits) - 1, padding_bits);
    }

    std::vector<std::uint8_t> take() && {
        return std::move(bytes);
    }

private:
    std::vector<std::uint8_t> bytes;
    std::uint64_t             accumulator = 0;
    unsigned                  pending_bits = 0;
};

Code literal_delta_code(std::uint8_t delta) {
    return LITERAL_DELTA_CODES[delta];
}

std::uint8_t near_match_tolerance(Resolution resolution, int run_length) {
    const int dpi = dots_per_inch(resolution);
    int       largest = dpi < 600 ? (dpi * 20 + 300) / 600 : dpi / 150 + 16;
    largest = std::min(largest, 255);
    const int tolerance = run_length != 0 ? dpi / 8 / run_length : largest;
    return static_cast<std::uint8_t>(std::clamp(tolerance, 0, largest));
}

std::uint8_t byte_at(std::uint32_t word, unsigned shift) {
    return static_cast<std::uint8_t>(word >> shift);
}

std::uint8_t absolute_difference(std::uint8_t first, std::uint8_t second) {
    return static_cast<std::uint8_t>(first >= second ? first - second : second - first);
}

std::uint32_t average_bytes(std::uint32_t first, std::uint32_t second) {
    return (first & second) + (((first ^ second) & 0xfefefefeu) >> 1);
}

using ErrorOffsets = std::array<int, 4>;

struct RgbFormat {
    using Pixel = std::uint32_t;
    using Error = std::uint32_t;

    static constexpr std::size_t  SOURCE_CHANNELS = RGB_CHANNELS;
    static constexpr Pixel        INITIAL_PIXEL = 0xffffffffu;
    static constexpr Pixel        PADDING_PIXEL = 0xffffffffu;
    static constexpr std::uint8_t CLEARED_MARKER_BYTE = 0xff;

    static int far_predictor_offset(int line_delta) {
        return line_delta + 2;
    }

    static void copy_line(std::span<const std::uint8_t> rgb, Pixel* line) {
        const std::uint8_t* source = rgb.data();
        const std::uint8_t* end = source + rgb.size();

        for (; source < end; source += SOURCE_CHANNELS) {
            const std::uint32_t red = source[0];
            const std::uint32_t green = source[1];
            const std::uint32_t blue = source[2];
            *line++ = 0xffu | red << 8 | green << 16 | blue << 24;
        }
    }

    static std::uint8_t marker_byte(Pixel pixel) {
        return byte_at(pixel, 0);
    }

    static void set_marker_byte(Pixel& pixel, std::uint8_t value) {
        pixel = (pixel & 0xffffff00u) | value;
    }

    static std::uint8_t difference(Pixel first, Pixel second) {
        const std::uint8_t red = absolute_difference(byte_at(first, 8), byte_at(second, 8));
        const std::uint8_t green = absolute_difference(byte_at(first, 16), byte_at(second, 16));
        const std::uint8_t blue = absolute_difference(byte_at(first, 24), byte_at(second, 24));
        return std::max({red, green, blue});
    }

    static Error error(Pixel original, Pixel replaced) {
        std::uint32_t difference = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            auto channel = static_cast<std::uint8_t>(byte_at(original, shift) - byte_at(replaced, shift));
            difference |= std::uint32_t{channel} << shift;
        }
        return difference;
    }

    static Pixel predict(const Error* error, const ErrorOffsets& offsets, Pixel pixel) {
        constexpr std::uint32_t BIAS = 0x80808080u;
        const auto              biased = [&](std::size_t weight) { return error[offsets[weight]] ^ BIAS; };

        std::uint32_t weighted_error = biased(3);
        weighted_error = average_bytes(weighted_error, biased(2));
        weighted_error = average_bytes(weighted_error, biased(1));
        weighted_error = average_bytes(weighted_error, biased(0));
        weighted_error ^= BIAS;

        Pixel predicted = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            const int channel = byte_at(pixel, shift) + static_cast<std::int8_t>(byte_at(weighted_error, shift));
            predicted |= static_cast<std::uint32_t>(std::clamp(channel, 0, 255)) << shift;
        }
        return predicted;
    }

    static void put_literal(BitWriter& writer, Pixel pixel, Pixel left) {
        std::uint64_t codes = 0;
        unsigned      bits = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            const Code code = literal_delta_code(static_cast<std::uint8_t>(byte_at(pixel, shift) - byte_at(left, shift)));
            codes = codes << code.bits | code.value;
            bits += code.bits;
        }
        writer.put(codes, bits);
    }
};

struct GrayFormat {
    using Pixel = std::uint8_t;
    using Error = std::int8_t;

    static constexpr std::size_t  SOURCE_CHANNELS = 1;
    static constexpr Pixel        INITIAL_PIXEL = 0x00;
    static constexpr Pixel        PADDING_PIXEL = 0xff;
    static constexpr std::uint8_t CLEARED_MARKER_BYTE = 0x00;

    static int far_predictor_offset(int) {
        return -8;
    }

    static void copy_line(std::span<const std::uint8_t> gray, Pixel* line) {
        std::ranges::copy(gray, line);
    }

    static std::uint8_t marker_byte(Pixel pixel) {
        return pixel;
    }

    static void set_marker_byte(Pixel& pixel, std::uint8_t value) {
        pixel = value;
    }

    static std::uint8_t difference(Pixel first, Pixel second) {
        return absolute_difference(first, second);
    }

    static Error error(Pixel original, Pixel replaced) {
        return static_cast<Error>(static_cast<std::uint8_t>(original - replaced));
    }

    static Pixel predict(const Error* error, const ErrorOffsets& offsets, Pixel pixel) {
        const int upper_right_errors = error[offsets[3]] + error[offsets[2]];
        const int adjacent_errors = error[offsets[1]] + error[offsets[0]] * 2;
        const int weighted_error = (upper_right_errors + adjacent_errors * 2) / 8;
        return static_cast<Pixel>(std::clamp(pixel + weighted_error, 0, 255));
    }

    static void put_literal(BitWriter& writer, Pixel pixel, Pixel left) {
        writer.put(literal_delta_code(static_cast<std::uint8_t>(pixel - left)));
    }
};

template <typename Format> class LineEncoder {
public:
    using Pixel = Format::Pixel;
    using Error = Format::Error;

    LineEncoder(std::uint32_t page_width, Resolution resolution)
        : source_width(static_cast<int>(page_width)), width(static_cast<int>(padded_width(page_width))), line_stride(width + LINE_PADDING) {
        const std::size_t storage_size = 2 * static_cast<std::size_t>(line_stride);
        pixel_storage.assign(storage_size, Format::INITIAL_PIXEL);
        error_storage.assign(storage_size, Error{0});
        for (std::size_t i = 0; i < predictor_order.size(); i++) predictor_order[i] = i;
        for (std::size_t run = 0; run < near_match_tolerances.size(); run++)
            near_match_tolerances[run] = near_match_tolerance(resolution, static_cast<int>(run));
    }

    void add_line(std::span<const std::uint8_t> line) {
        const std::size_t expected_size = static_cast<std::size_t>(source_width) * Format::SOURCE_CHANNELS;
        if (line.size() != expected_size) throw std::invalid_argument("the line length does not match the page width");

        const int current_buffer = line_number & 1;
        const int previous_buffer = current_buffer ^ 1;
        Pixel*    pixel = pixel_storage.data() + LINE_MARGIN + current_buffer * line_stride;
        Error*    error = error_storage.data() + LINE_MARGIN + current_buffer * line_stride;
        Pixel*    line_end = pixel + width;
        Error     last_match_error = 0;

        update_offsets((previous_buffer - current_buffer) * line_stride);
        Format::copy_line(line, pixel);
        std::fill(pixel + source_width, line_end, Format::PADDING_PIXEL);
        std::fill_n(error, width, Error{0});
        table_balance = 0;
        place_line_end_marker(line_end);

        while (pixel < line_end) {
            const int tolerance = pending_run < NEAR_MATCH_RUN_LIMIT ? near_match_tolerances[static_cast<std::size_t>(pending_run)] : 0;
            Run       run{};

            if (tolerance == 0 || last_match_error == 0) {
                if (line_end - pixel < MARKER_REFRESH_DISTANCE) place_line_end_marker(line_end);
                run = longest_run(pixel);
                if (run.length == 0 && tolerance == 0) {
                    put_literal(pixel);
                    pixel++;
                    error++;
                    continue;
                }
            }
            if (run.length == 0) {
                const std::optional<int> predictor = nearest_predictor(pixel, error, tolerance);
                if (!predictor) {
                    *pixel = Format::predict(error, error_neighbor_offsets, *pixel);
                    put_literal(pixel);
                    pixel++;
                    error++;
                    continue;
                }
                run = {.order_index = *predictor, .length = 1};
                last_match_error = *error;
            }
            extend_run(run);
            pixel += run.length;
            error += run.length;
        }

        Format::set_marker_byte(*line_end, Format::CLEARED_MARKER_BYTE);
        update_code_table();
        line_number++;
    }

    std::vector<std::uint8_t> finish() {
        if (pending_run != 0) put_run(pending_symbol, pending_run);
        writer.put(symbol_code(ESCAPE_SYMBOL));
        writer.put(END_OF_PAGE);
        writer.pad_with_ones();
        return std::move(writer).take();
    }

private:
    struct Run {
        int order_index;
        int length;
    };

    Code symbol_code(std::size_t symbol) const {
        return SYMBOL_CODES[code_table][symbol];
    }

    int predictor_offset(int order_index) const {
        return predictor_offsets[predictor_order[static_cast<std::size_t>(order_index)]];
    }

    void update_offsets(int line_delta) {
        predictor_offsets = {-1, line_delta, line_delta - 1, line_delta + 1, Format::far_predictor_offset(line_delta)};
        error_neighbor_offsets = {-1, line_delta, line_delta + 1, line_delta + 3};
    }

    void place_line_end_marker(Pixel* line_end) {
        std::uint8_t mask = 0;
        for (std::size_t predictor = 0; predictor < predictor_offsets.size(); predictor++) {
            const std::uint8_t reference = Format::marker_byte(line_end[predictor_offsets[predictor]]);
            const std::uint8_t bit = static_cast<std::uint8_t>(1u << predictor);
            mask = static_cast<std::uint8_t>(mask | (~reference & bit));
        }
        Format::set_marker_byte(*line_end, mask);
    }

    Run longest_run(const Pixel* pixel) const {
        Run longest{.order_index = 0, .length = 0};
        for (int order_index = 0; order_index < PREDICTOR_COUNT; order_index++) {
            const Pixel* scan = pixel;
            const Pixel* reference = pixel + predictor_offset(order_index);
            while (*scan == *reference) {
                scan++;
                reference++;
            }
            const int length = static_cast<int>(scan - pixel);
            if (length > longest.length) longest = {.order_index = order_index, .length = length};
        }
        return longest;
    }

    std::optional<int> nearest_predictor(Pixel* pixel, Error* error, int tolerance) {
        const Pixel original = *pixel;
        auto        threshold = static_cast<std::uint8_t>(tolerance + 1);
        int         nearest = 0;

        for (int order_index = 0; order_index < PREDICTOR_COUNT; order_index++) {
            const std::uint8_t difference = Format::difference(original, pixel[predictor_offset(order_index)]);
            if (difference < threshold) {
                nearest = order_index;
                threshold = difference;
            }
        }
        if (std::cmp_greater(threshold, tolerance)) return std::nullopt;

        *pixel = pixel[predictor_offset(nearest)];
        *error = Format::error(original, *pixel);
        return nearest;
    }

    void put_run(int symbol, int length) {
        writer.put(symbol_code(static_cast<std::size_t>(symbol)));
        if (length < SHORT_RUN_LIMIT) {
            writer.put(SHORT_RUN_CODES[static_cast<std::size_t>(length)]);
            return;
        }

        const auto        group_end = std::ranges::upper_bound(LONG_RUN_GROUP_STARTS, length);
        const auto        group = static_cast<std::size_t>(group_end - LONG_RUN_GROUP_STARTS.begin() - 1);
        const auto        extra_value = static_cast<std::uint32_t>(length - LONG_RUN_GROUP_STARTS[group]);
        const std::size_t group_code = LONG_RUN_GROUP_BASE + group;
        writer.put(static_cast<std::uint32_t>(group_code), LONG_RUN_GROUP_BITS);
        writer.put(extra_value, LONG_RUN_EXTRA_BITS[group]);
    }

    void put_literal(const Pixel* pixel) {
        if (pending_run != 0) {
            put_run(pending_symbol, pending_run);
            pending_run = 0;
        }
        writer.put(symbol_code(LITERAL_SYMBOL));
        Format::put_literal(writer, pixel[0], pixel[-1]);
        table_balance--;
        after_partial_run = false;
    }

    void extend_run(Run run) {
        if (pending_run != 0 && run.order_index == 0) {
            pending_run += run.length;
        } else {
            if (pending_run != 0) {
                put_run(pending_symbol, pending_run);
                after_partial_run = true;
            }
            pending_symbol = run.order_index - (after_partial_run ? 1 : 0);
            if (pending_symbol == 0) table_balance++;
            pending_run = run.length;
            const auto first = predictor_order.begin();
            std::rotate(first, first + run.order_index, first + run.order_index + 1);
        }

        while (pending_run >= LONGEST_RUN) {
            put_run(pending_symbol, LONGEST_RUN);
            table_balance++;
            pending_symbol = 0;
            after_partial_run = false;
            pending_run -= LONGEST_RUN;
        }
    }

    void update_code_table() {
        if (table_balance >= -TABLE_SWITCH_THRESHOLD && table_balance <= TABLE_SWITCH_THRESHOLD) return;

        const std::size_t next_table = table_balance < 0 ? 1u : 0u;
        if (next_table == code_table) return;
        code_table = next_table;
        writer.put(symbol_code(ESCAPE_SYMBOL));
        writer.put(TABLE_SWITCH);
    }

    int                                            source_width;
    int                                            width;
    int                                            line_stride;
    int                                            line_number = 0;
    int                                            pending_run = 0;
    int                                            pending_symbol = 0;
    bool                                           after_partial_run = false;
    int                                            table_balance = 0;
    std::size_t                                    code_table = 0;
    std::array<std::uint8_t, NEAR_MATCH_RUN_LIMIT> near_match_tolerances{};
    std::array<std::size_t, PREDICTOR_COUNT>       predictor_order{};
    std::array<int, PREDICTOR_COUNT>               predictor_offsets{};
    ErrorOffsets                                   error_neighbor_offsets{};
    std::vector<Pixel>                             pixel_storage;
    std::vector<Error>                             error_storage;
    BitWriter                                      writer;
};

}

class Compressor::Encoder {
public:
    template <typename Format>
    Encoder(std::type_identity<Format>, std::uint32_t width, Resolution resolution)
        : line_encoder(std::in_place_type<LineEncoder<Format>>, width, resolution) {}

    std::variant<LineEncoder<RgbFormat>, LineEncoder<GrayFormat>> line_encoder;
};

Compressor::Compressor(ColorMode mode, std::uint32_t width, Resolution resolution) {
    if (width > MAX_LINE_WIDTH) throw std::length_error("the page is too wide to compress");
    if (mode == ColorMode::COLOR)
        encoder = std::make_unique<Encoder>(std::type_identity<RgbFormat>(), width, resolution);
    else
        encoder = std::make_unique<Encoder>(std::type_identity<GrayFormat>(), width, resolution);
}

Compressor::Compressor(Compressor&&) noexcept = default;
Compressor& Compressor::operator=(Compressor&&) noexcept = default;
Compressor::~Compressor() = default;

void Compressor::add_line(std::span<const std::uint8_t> line) {
    std::visit([line](auto& line_encoder) { line_encoder.add_line(line); }, encoder->line_encoder);
}

std::vector<std::uint8_t> Compressor::finish() && {
    return std::visit([](auto& line_encoder) { return line_encoder.finish(); }, encoder->line_encoder);
}

std::vector<std::uint8_t> compress(ColorMode mode, std::span<const std::uint8_t> pixels, std::uint32_t width, Resolution resolution) {
    const std::size_t channels = channel_count(mode);
    const std::size_t line_size = std::size_t{width} * channels;
    Compressor        compressor(mode, width, resolution);

    if (line_size == 0) return std::move(compressor).finish();
    if (pixels.size() % line_size != 0) throw std::invalid_argument("the pixel data is not a whole number of lines");
    for (std::size_t offset = 0; offset < pixels.size(); offset += line_size) compressor.add_line(pixels.subspan(offset, line_size));
    return std::move(compressor).finish();
}

}
