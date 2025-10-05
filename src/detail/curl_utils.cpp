#include "downloader/detail/curl_utils.hpp"

#include <curl/curl.h>
#include <cstdlib>
#include <stdexcept>
#include <mutex>

namespace downloader::detail {

void ensureCurlInitialized() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error("Failed to initialize libcurl");
        }
        std::atexit([] { curl_global_cleanup(); });
    });
}

} // namespace downloader::detail