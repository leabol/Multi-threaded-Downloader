#include "downloader/download_manager.hpp"
#include "downloader/multi_downloader.hpp"
#include "downloader/detail/curl_utils.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>


namespace {
void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName
              << " [-d <directory>] [-t <threads>] <url1> <file1> [<url2> <file2> ...]"
              << std::endl;
    std::cerr << "Options:\n"
              << "  -d <directory>   Set download directory (default: current directory)\n"
              << "  -t <threads>     Number of threads per download task (default: 8)\n"
              << "  -h, --help       Show this message" << std::endl;
}
} // namespace

int main(int argc, char** argv) {
    try {
        downloader::detail::ensureCurlInitialized();
        int threads = 8;      //默认线程数
        std::filesystem::path download_dir = std::filesystem::current_path();   // 默认下载路径为当前路径下
        int arg_index = 1;

        while (arg_index < argc && argv[arg_index][0] == '-') {
            const std::string option = argv[arg_index];

            if (option == "-d") {
                if (arg_index + 1 >= argc) {
                    printUsage(argv[0]);
                    return 1;
                }

                download_dir = argv[arg_index + 1];
                std::error_code ec;
                std::filesystem::create_directories(download_dir, ec);
                if (ec) {
                    throw std::runtime_error("Failed to create download directory: "
                         + download_dir.string() + " - " + ec.message());
                }
                arg_index += 2;
            } else if (option == "-t") {
                if (arg_index + 1 >= argc) {
                    printUsage(argv[0]);
                    return 1;
                }

                try {
                    threads = std::stoi(argv[arg_index + 1]);
                } catch (const std::exception&) {
                    throw std::runtime_error("Invalid thread count: " + std::string(argv[arg_index + 1]));
                }

                if (threads <= 0 || threads > 65) {
                    throw std::runtime_error("Thread count is invalid.");
                }

                arg_index += 2;
            } else if (option == "-h" || option == "--help") {
                printUsage(argv[0]);
                return 0;
            } else {
                printUsage(argv[0]);
                return 1;
            }
        }

        if (argc - arg_index < 2 || (argc - arg_index) % 2 != 0) {
            printUsage(argv[0]);
            return 1;
        }

        //初始化下载管理器，添加任务
        downloader::DownloadManager manager;
        for (int i = arg_index; i < argc; i += 2) {
            std::filesystem::path destination = download_dir / argv[i + 1];
            auto downloader_task = std::make_shared<downloader::MultiDownloader>(
                argv[i], destination.string(), threads
            );
            manager.addTask(std::move(downloader_task));
        }

        //开始下载
        manager.start();
        //打印错误信息
        manager.printError();
        
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return 1;
    }
}