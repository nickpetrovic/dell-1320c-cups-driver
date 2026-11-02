#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "print_format.hpp"
#include "settings.hpp"

namespace dell1320c::hbpl {

struct Page {
    std::uint32_t             width = 0;
    std::uint32_t             height = 0;
    Resolution                resolution = Resolution::DPI_600;
    ColorMode                 color_mode = ColorMode::COLOR;
    std::vector<std::uint8_t> compressed;
};

struct HostIdentity {
    std::string name;
    std::string address;
};

struct Job {
    std::string  user;
    std::string  title;
    HostIdentity host;
    std::time_t  time = 0;
};

[[nodiscard]] std::string host_address_text(const std::array<std::uint8_t, 4>& octets);
[[nodiscard]] HostIdentity host_identity();
[[nodiscard]] std::string printable_text(std::string_view text);

enum class JobEnd : std::uint8_t {
    COMPLETE,
    INTERRUPTED,
};

class JobWriter {
public:
    JobWriter(std::FILE* stream, const Settings& print_settings, Job print_job);
    JobWriter(std::FILE*, Settings&&, Job) = delete;
    JobWriter(const JobWriter&) = delete;
    JobWriter& operator=(const JobWriter&) = delete;
    JobWriter(JobWriter&&) = delete;
    JobWriter& operator=(JobWriter&&) = delete;
    ~JobWriter() = default;

    void begin();
    void add_page(Page page);
    void end(JobEnd job_end);

private:
    void write_header() const;
    void write_color_attributes() const;
    void write_page(const Page& page, std::uint32_t position_in_set);
    void write_trailer() const;
    void write_bytes(std::span<const std::uint8_t> bytes) const;
    void flush() const;
    [[nodiscard]] int sets() const;

    std::FILE*             output;
    const Settings&        settings;
    Job                    job;
    std::uint32_t          page_number = 0;
    int                    written_sets = 1;
    std::vector<ColorMode> page_color_modes;
    std::vector<Page>      repeated_pages;
};

}
