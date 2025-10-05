#pragma once

#include "progress.hpp"

#include <memory>

namespace downloader {

class DownloadTask {
public:
    virtual ~DownloadTask() = default;

    virtual void start() = 0;
    [[nodiscard]] virtual Progress getProgress() const = 0;
    [[nodiscard]] virtual bool isRunning() const = 0;
    [[nodiscard]] virtual bool hasError() const = 0;
};

using DownloadTaskPtr = std::shared_ptr<DownloadTask>;

} // namespace downloader
