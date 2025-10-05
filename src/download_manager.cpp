#include "downloader/download_manager.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <filesystem>

#include <fmt/format.h>

namespace downloader {

void DownloadManager::addTask(DownloadTaskPtr task) {
    if (task) {
        tasks_.push_back(std::move(task));
    }
}

void DownloadManager::start() {
    threads_.reserve(tasks_.size());
    for (auto& task : tasks_) {
        threads_.emplace_back([task]() {
            if (task) {
                task->start();
            }
        });
    }

    renderProgressLoop();

    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();
}

void DownloadManager::renderProgressLoop() {
    std::size_t previous_lines = 0;
    while (true) {
        const auto panel = buildProgressPanel();
        redrawPanel(panel, previous_lines);

        if (!hasActiveTasks()) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << std::flush;
}

std::string DownloadManager::buildProgressPanel() const {
    std::string panel;
    panel.reserve(tasks_.size() * 128 + 256);
    panel.append("==================================================\n");
    panel += fmt::format("Download Manager ({} tasks)\n", tasks_.size());
    panel.append("--------------------------------------------------\n");

    std::uint64_t total_all = 0;
    std::uint64_t downloaded_all = 0;

    for (const auto& task : tasks_) {
        if (!task) {
            continue;
        }

        const auto progress = task->getProgress();
        panel += formatTaskLine(progress);
        panel.push_back('\n');

        total_all += progress.total_bytes;
        downloaded_all += progress.downloaded_bytes;
    }

    panel.append("--------------------------------------------------\n");
    if (total_all > 0) {
        const double ratio = static_cast<double>(downloaded_all) / static_cast<double>(total_all);
        panel += fmt::format("Overall: {:>3}%", static_cast<int>(ratio * 100.0));
    } else {
        panel.append("Overall: N/A");
    }
    panel.push_back('\n');
    panel.append("==================================================\n");

    return panel;
}

std::string DownloadManager::formatTaskLine(const Progress& progress) {
    std::string line;
    line.reserve(256);

        std::string display_name;
        if (!progress.filename.empty()) {
            std::filesystem::path path{progress.filename};
            display_name = path.filename().string();
        }
        if (display_name.empty()) {
            display_name = progress.filename;
        }
        if (display_name.size() > 20) {
            display_name = display_name.substr(0, 20);
        }
        if (display_name.empty()) {
            display_name = "(unnamed)";
        }

    if (progress.total_bytes > 0) {
        const double ratio = static_cast<double>(progress.downloaded_bytes) /
                             static_cast<double>(progress.total_bytes);
        const int percent = static_cast<int>(ratio * 100.0);
        constexpr int bar_width = 30;
        const int bar_pos = static_cast<int>(ratio * bar_width);

        std::string bar;
        bar.reserve(static_cast<std::size_t>(bar_width) * 3);
        for (int i = 0; i < bar_width; ++i) {
            bar += (i < bar_pos) ? u8"█" : u8"░";
        }

            line += fmt::format("{:<20} [{}] {:>3}% ({}/{})",
                                display_name,
                            bar,
                            percent,
                            formatSize(progress.downloaded_bytes),
                            formatSize(progress.total_bytes));

        if (progress.has_error) {
            line += fmt::format("  ❌ {}", progress.error_message);
        } else if (!progress.is_running) {
            line.append("  ✅ Done");
        }
    } else {
            line += fmt::format("{:<20} [Initializing...]", display_name);
    }

    return line;
}

std::string DownloadManager::formatSize(std::uint64_t bytes) {
    constexpr double KB = 1024.0;
    constexpr double MB = KB * 1024.0;
    constexpr double GB = MB * 1024.0;

    const double value = static_cast<double>(bytes);
    if (bytes >= static_cast<std::uint64_t>(GB)) {
        return fmt::format("{:.1f} GB", value / GB);
    } else if (bytes >= static_cast<std::uint64_t>(MB)) {
        return fmt::format("{:.1f} MB", value / MB);
    } else if (bytes >= static_cast<std::uint64_t>(KB)) {
        return fmt::format("{:.1f} KB", value / KB);
    } else {
        return fmt::format("{} B", bytes);
    }
}

bool DownloadManager::hasActiveTasks() const {
    for (const auto& task : tasks_) {
        if (!task) {
            continue;
        }

        const auto progress = task->getProgress();
        if (progress.has_error) {
            continue;
        }
        if (progress.is_running) {
            return true;
        }
        if (progress.total_bytes > 0 && progress.downloaded_bytes < progress.total_bytes) {
            return true;
        }
    }
    return false;
}

void DownloadManager::redrawPanel(const std::string& panel, std::size_t& previous_lines) {
    const std::size_t current_lines = static_cast<std::size_t>(std::count(panel.begin(), panel.end(), '\n'));
    if (previous_lines > 0) {
        std::cout << "\033[" << previous_lines << "F\033[J";
    }
    std::cout << panel;
    previous_lines = current_lines;
}

} // namespace downloader
