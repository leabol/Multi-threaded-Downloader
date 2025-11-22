#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "downloader/detail/curl_utils.hpp"
#include "downloader/download_manager.hpp"
#include "downloader/multi_downloader.hpp"

namespace {
constexpr int kDefaultThreadCount = 8;
constexpr int kMaxThreadCount     = 65;

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName
              << " [-d <directory>] [-t <threads>] <url1> <file1> [<url2> <file2> ...]"
              << std::endl;
    std::cerr << "Options:\n"
              << "  -d <directory>   Set download directory (default: current directory)\n"
              << "  -t <threads>     Number of threads per download task (default: 8)\n"
              << "  -h, --help       Show this message" << std::endl;
}

void ensureDirectoryExists(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        throw std::runtime_error("Failed to create download directory: " + directory.string() +
                                 " - " + ec.message());
    }
}

std::vector<std::pair<std::string, std::string>> promptInteractiveTasks(
    std::filesystem::path& download_dir, int& threads) {
    std::vector<std::pair<std::string, std::string>> tasks;
    std::string                                      input;

    std::cout << "Interactive download setup" << std::endl;
    std::cout << "Download directory [" << download_dir.string() << "]: " << std::flush;
    if (!std::getline(std::cin, input)) {
        return tasks;
    }
    if (!input.empty()) {
        download_dir = input;
    }
    ensureDirectoryExists(download_dir);

    while (true) {
        std::cout << "Thread count [" << threads << "]: " << std::flush;
        if (!std::getline(std::cin, input)) {
            return tasks;
        }
        if (input.empty()) {
            break;
        }
        try {
            const int value = std::stoi(input);
            if (value > 0 && value <= kMaxThreadCount) {
                threads = value;
                break;
            }
        } catch (const std::exception&) {
        }
        std::cout << "Invalid thread count, please enter a value between 1 and 65." << std::endl;
    }

    while (true) {
        std::cout << "Enter download URL (empty to finish): " << std::flush;
        std::string url;
        if (!std::getline(std::cin, url) || url.empty()) {
            break;
        }

        std::cout << "Save as filename: " << std::flush;
        std::string filename;
        if (!std::getline(std::cin, filename)) {
            break;
        }
        if (filename.empty()) {
            std::cout << "Filename cannot be empty." << std::endl;
            continue;
        }

        tasks.emplace_back(std::move(url), std::move(filename));
    }

    return tasks;
}

enum class ParseStatus { kOk, kShowHelp, kError };

ParseStatus handleOption(const std::string&     option,
                         int                    argc,
                         char**                 argv,
                         int&                   arg_index,
                         std::filesystem::path& download_dir,
                         int&                   threads) {
    if (option == "-d") {
        if (arg_index + 1 >= argc) {
            return ParseStatus::kError;
        }

        download_dir = argv[arg_index + 1];
        ensureDirectoryExists(download_dir);
        arg_index += 2;
        return ParseStatus::kOk;
    }

    if (option == "-t") {
        if (arg_index + 1 >= argc) {
            return ParseStatus::kError;
        }

        try {
            threads = std::stoi(argv[arg_index + 1]);
        } catch (const std::exception&) {
            return ParseStatus::kError;
        }

        if (threads <= 0 || threads > kMaxThreadCount) {
            return ParseStatus::kError;
        }

        arg_index += 2;
        return ParseStatus::kOk;
    }

    if (option == "-h" || option == "--help") {
        return ParseStatus::kShowHelp;
    }

    return ParseStatus::kError;
}

ParseStatus parseCommandLine(int                                               argc,
                             char**                                            argv,
                             std::filesystem::path&                            download_dir,
                             int&                                              threads,
                             std::vector<std::pair<std::string, std::string>>& task_specs) {
    int arg_index = 1;

    while (arg_index < argc && argv[arg_index][0] == '-') {
        const ParseStatus option_status =
            handleOption(argv[arg_index], argc, argv, arg_index, download_dir, threads);
        if (option_status != ParseStatus::kOk) {
            return option_status;
        }
    }

    const int remaining = argc - arg_index;
    if (remaining > 0) {
        if (remaining % 2 != 0) {
            return ParseStatus::kError;
        }

        for (int i = arg_index; i < argc; i += 2) {
            task_specs.emplace_back(argv[i], argv[i + 1]);
        }
    }

    return ParseStatus::kOk;
}
}  // namespace

int main(int argc, char** argv) {
    try {
        downloader::detail::ensureCurlInitialized();
        int                   threads = kDefaultThreadCount;
        std::filesystem::path download_dir =
            std::filesystem::current_path();  // 默认下载路径为当前路径下
        std::vector<std::pair<std::string, std::string>> task_specs;
        const ParseStatus status = parseCommandLine(argc, argv, download_dir, threads, task_specs);

        if (status == ParseStatus::kShowHelp) {
            printUsage(argv[0]);
            return 0;
        }
        if (status == ParseStatus::kError) {
            printUsage(argv[0]);
            return 1;
        }

        if (task_specs.empty()) {
            task_specs = promptInteractiveTasks(download_dir, threads);
        }

        if (task_specs.empty()) {
            std::cerr << "No download tasks were provided." << std::endl;
            return 1;
        }

        // 初始化下载管理器，添加任务
        downloader::DownloadManager manager;
        for (const auto& [url, filename] : task_specs) {
            std::filesystem::path destination = download_dir / filename;
            auto                  downloader_task =
                std::make_shared<downloader::MultiDownloader>(url, destination.string(), threads);
            manager.addTask(std::move(downloader_task));
        }

        // 开始下载
        manager.start();
        // 打印错误信息
        manager.printError();

    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return 1;
    }
}