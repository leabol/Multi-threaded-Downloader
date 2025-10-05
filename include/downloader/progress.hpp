#pragma once

#include <cstdint>
#include <string>

namespace downloader {

struct Progress {
    std::string url;
    std::string filename;
    std::uint64_t total_bytes{0};
    std::uint64_t downloaded_bytes{0};
    bool is_running{false};
    bool has_error{false};
    std::string error_message;
};

} // namespace downloader
