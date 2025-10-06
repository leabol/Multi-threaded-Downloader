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
    << " [-d <directory>] <url1> <file1> [<url2> <file2> ...]" << std::endl;
    std::cerr << "Each file name will be stored under ~/download by default." << std::endl;
}

// 使用默认下载路径
std::filesystem::path resolveDownloadDirectory() {
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home && *home ? 
        std::filesystem::path(home) : std::filesystem::current_path();

    std::filesystem::path target = base / "download";

    std::error_code ec;
    std::filesystem::create_directories(target, ec);
    if (ec) {
        throw std::runtime_error("Failed to create download directory: "
             + target.string() + " - " + ec.message());
    }

    return target;
}

} // namespace

int main(int argc, char** argv) {
    try {
        downloader::detail::ensureCurlInitialized();
        constexpr int default_threads = 8;   //默认线程数
        std::filesystem::path download_dir;
        int arg_start = 1;

        //处理处理文件路径, 和参数错误
        if (argc > 3 && std::string(argv[1]) == "-d") {
            download_dir = argv[2];
            arg_start = 3;

            std::error_code ec;
            std::filesystem::create_directories(download_dir, ec);
            if (ec) {
                throw std::runtime_error("Failed to create download directory: "
                     + download_dir.string() + " - " + ec.message());
            }
        } else {
            download_dir = resolveDownloadDirectory();
        }

        if (argc - arg_start < 2 || (argc - arg_start) % 2 != 0) {
            printUsage(argv[0]);
            return 1;
        }

        //初始化下载管理器，添加任务
        downloader::DownloadManager manager;
        for (int i = arg_start; i < argc; i += 2) {
            std::filesystem::path destination = download_dir / argv[i + 1];
            auto downloader_task = std::make_shared<downloader::MultiDownloader>(
                argv[i], destination.string(), default_threads
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