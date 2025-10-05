#pragma once

#include "download_task.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace downloader {

class DownloadManager {
public:
    void addTask(DownloadTaskPtr task);
    void start();

private:
    void renderProgressLoop();
    std::string buildProgressPanel() const;
    static std::string formatTaskLine(const Progress& progress);
    static std::string formatSize(std::uint64_t bytes);
    bool hasActiveTasks() const;
    void redrawPanel(const std::string& panel, std::size_t& previous_lines);

    std::vector<std::thread> threads_;
    std::vector<DownloadTaskPtr> tasks_;
};

} // namespace downloader
