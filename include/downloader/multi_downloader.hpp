#pragma once

#include "download_task.hpp"

#include <memory>
#include <string>


namespace downloader {
    
    class MultiDownloader final : public DownloadTask {
    public:
        MultiDownloader(std::string url, std::string destination, int thread_count = 8);
        ~MultiDownloader() override;

        void start() override;
        [[nodiscard]] Progress getProgress() const override;
        [[nodiscard]] bool isRunning() const override;
        [[nodiscard]] bool hasError() const override;

    private:
        //使用impl类减少头文件的依赖,提高编译速度, 使接口更安全稳定
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace downloader
