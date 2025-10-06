#include "downloader/multi_downloader.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <curl/curl.h>
#include <unistd.h>

namespace downloader {

class MultiDownloader::Impl {
public:
    Impl(std::string url, std::string destination, int thread_count)
        : url_(std::move(url)), 
        destination_(std::move(destination)),
        thread_count_(std::max(1, thread_count)){}

    ~Impl() { resetState(); }

    void start() {
        resetState();

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            is_running_ = true;
            has_error_ = false;
            error_message_.clear();
            downloaded_bytes_ = 0;
            total_bytes_ = 0;
        }

        file_.reset(std::fopen(destination_.c_str(), "wb+"));
        if (!file_) {
            registerError("Cannot create destination file");
            return;
        }

        const auto metadata = fetchMetadata();
        if (!metadata.supports_range || metadata.content_length == 0) {
            simplDownload();

            if (file_) {
                std::fflush(file_.get());
                file_.reset();
            }

            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                if (total_bytes_ == 0) {
                    total_bytes_ = downloaded_bytes_;
                }
            }

            setRunning(false);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            total_bytes_ = metadata.content_length;
            downloaded_bytes_ = 0;
        }

        if (ftruncate(fileno(file_.get()), total_bytes_) == -1) {
            file_.reset();
            registerError("Cannot resize destination file");
            return;
        }

        workers_.reserve(thread_count_);
        const curl_off_t part_size = std::max<curl_off_t>(1, (total_bytes_ + thread_count_ - 1) / thread_count_);
        for (int i = 0; i < thread_count_; ++i) {
            const curl_off_t start = static_cast<curl_off_t>(i) * part_size;
            const curl_off_t end = std::min(start + part_size, total_bytes_);
            if (start >= total_bytes_) {
                break;
            }

            workers_.emplace_back([this, start, end]() { downloadRange(start, end); });
        }

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();

        if (file_) {
            std::fflush(file_.get());
            file_.reset();
        }

        setRunning(false);
    }

    [[nodiscard]] Progress getProgress() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return {
            url_, 
            destination_, 
            static_cast<std::uint64_t>(total_bytes_), 
            static_cast<std::uint64_t>(downloaded_bytes_), 
            is_running_, 
            has_error_,
            error_message_
        };
    }

    [[nodiscard]] bool isRunning() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return is_running_;
    }

    [[nodiscard]] bool hasError() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return has_error_;
    }

private:
    struct FileDeleter {
        void operator()(FILE* fp) const noexcept {
            if (fp) {
                std::fclose(fp);
            }
        }
    };

    struct FileMetadata {
        bool supports_range{false};
        curl_off_t content_length{0};
    };

    struct RangeContext {
        Impl* owner{nullptr};
        curl_off_t start{0};
        curl_off_t hasWritten{0};
    };

    [[nodiscard]] FileMetadata fetchMetadata() const {
        using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;

        FileMetadata meta;
        CurlHandle curl{curl_easy_init(), &curl_easy_cleanup};
        if (!curl) {
            return meta;
        }

        curl_easy_setopt(curl.get(), CURLOPT_URL, url_.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_HEADER, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);

        std::string headers;
        curl_easy_setopt( curl.get(), CURLOPT_WRITEFUNCTION,
            +[](char* ptr, size_t size, size_t nmemb, std::string* out) -> size_t {
                if (!out) {
                    return 0;
                }
                out->append(ptr, size * nmemb);
                return size * nmemb;
            });
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &headers);

        if (curl_easy_perform(curl.get()) == CURLE_OK) {
            long code = 0;
            curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &code);
            meta.supports_range = (code == 200 || code == 206);

            if (headers.find("Accept-Ranges: bytes") != std::string::npos) {
                meta.supports_range = true;
            }

            curl_off_t length = 0;
            curl_easy_getinfo(curl.get(), CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
            //保证不为负数, 如果没有返回length字段的值, 将返回-1
            meta.content_length = std::max<curl_off_t>(0, length); 
        }

        return meta;
    }

    void downloadRange(curl_off_t start, curl_off_t end) {
        using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;

        CurlHandle curl{curl_easy_init(), &curl_easy_cleanup};
        if (!curl) {
            registerError("Failed to allocate curl handle", false);
            return;
        }

        RangeContext ctx{this, start, 0};
        const std::string range = std::to_string(start) + "-" + std::to_string(end - 1);

        curl_easy_setopt(curl.get(), CURLOPT_URL, url_.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_RANGE, range.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, &Impl::writeCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 1L);

        const CURLcode res = curl_easy_perform(curl.get());
        if (res != CURLE_OK) {
            registerError(std::string{"curl error: "} + curl_easy_strerror(res), false);
            return;
        }

        const curl_off_t expected = end - start;
        if (ctx.hasWritten != expected) {
            registerError("Range download incomplete", false);
        }
    }

    void simplDownload() {
        using CurlHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;

        CurlHandle curl{curl_easy_init(), &curl_easy_cleanup};
        if (!curl) {
            registerError("Failed to allocate curl handle", false);
            return;
        }

        RangeContext ctx{this, 0, 0};
        curl_easy_setopt(curl.get(), CURLOPT_URL, url_.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, &Impl::writeCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);

        const CURLcode res = curl_easy_perform(curl.get());
        if (res != CURLE_OK) {
            registerError(std::string{"curl error: "} + curl_easy_strerror(res), false);
            return;
        }

    }

    static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* ctx = static_cast<RangeContext*>(userdata);
        if (!ctx || !ctx->owner) {
            return 0;
        }

        Impl& self = *ctx->owner;
        const size_t total = size * nmemb;
        if (total == 0) {
            return 0;
        }

        std::lock_guard<std::mutex> file_lock(self.file_mutex_);
        FILE* file = self.file_.get();
        if (!file) {
            return 0;
        }

        if (fseeko(file, ctx->start + ctx->hasWritten, SEEK_SET) != 0) {
            self.registerError("Failed to seek output file", false);
            return 0;
        }

        const size_t written = std::fwrite(ptr, 1, total, file);
        if (written != total) {
            self.registerError("Failed to write output file", false);
            return written;
        }

        ctx->hasWritten += static_cast<curl_off_t>(written);
        {
            std::lock_guard<std::mutex> state_lock(self.state_mutex_);
            self.downloaded_bytes_ += static_cast<curl_off_t>(written);
        }

        return written;
    }

    void resetState() {
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
        file_.reset();

        std::lock_guard<std::mutex> lock(state_mutex_);
        total_bytes_ = 0;
        downloaded_bytes_ = 0;
        has_error_ = false;
        error_message_.clear();
        is_running_ = false;
    }

    void registerError(std::string message, bool stop_immediately = true) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        has_error_ = true;
        if (error_message_.empty()) {
            error_message_ = std::move(message);
        }
        if (stop_immediately) {
            is_running_ = false;
        }
    }

    void setRunning(bool running) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        is_running_ = running;
    }

    std::string url_;
    std::string destination_;
    int thread_count_;

    std::unique_ptr<FILE, FileDeleter> file_{};
    std::vector<std::thread> workers_;

    mutable std::mutex state_mutex_;
    mutable std::mutex file_mutex_;

    curl_off_t total_bytes_{0};
    curl_off_t downloaded_bytes_{0};
    bool is_running_{false};
    bool has_error_{false};
    std::string error_message_;
};

MultiDownloader::MultiDownloader(std::string url, std::string destination, int thread_count)
    : impl_(std::make_unique<Impl>(std::move(url), std::move(destination), thread_count)) {}

MultiDownloader::~MultiDownloader() = default;

void MultiDownloader::start() { impl_->start(); }

Progress MultiDownloader::getProgress() const { return impl_->getProgress(); }

bool MultiDownloader::isRunning() const { return impl_->isRunning(); }

bool MultiDownloader::hasError() const { return impl_->hasError(); }

} // namespace downloader
