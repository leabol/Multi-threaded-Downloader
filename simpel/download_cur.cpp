#include <iostream>      
#include <curl/curl.h>  
#include <fstream>       
#include <memory>       
#include <string>       
#include <vector>       
#include <thread>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace {
    struct CurlGlobalInitializer {
        CurlGlobalInitializer() {
            if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
                throw std::runtime_error("Failed to initialize libcurl");
            }
        }
        ~CurlGlobalInitializer() {
            curl_global_cleanup();
        }
    };
    // 全局实例 → 构造/析构自动触发
    CurlGlobalInitializer g_curl_initializer;
}

class Downloader {
public:
    //初始化对象时,有些操作可能会出错, 但是初始化函数没有返回值来检查, 
    //可以通过抛出异常来解决 ,或者使用函数工厂将初始化函数和init函数(真正分配资源)包装起来
    explicit Downloader(const std::string& url, const std::string& outfile)
        : url_(url), outputFilename_(outfile), file_(outfile), curl_(curl_easy_init()){
        if (!file_.is_open()) {
            throw std::runtime_error("Failed to open file");
        }
        if (!curl_) {
            file_.close();
            throw std::runtime_error("Failed to init curl");
        }
        // 设置 curl 选项
        // CURLOPT_URL: 要下载的目标 URL 地址
        curl_easy_setopt(curl_, CURLOPT_URL, url_.c_str());
        // CURLOPT_WRITEFUNCTION: 下载数据到本地时调用的回调函数（WriteCallback）
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, Downloader::WriteCallback);
        // CURLOPT_WRITEDATA: 传递给回调函数的第四个参数（这里是输出文件流指针 &file）
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &file_);
        // CURLOPT_FOLLOWLOCATION: 是否自动跟随 HTTP 3xx 重定向（1L 表示开启）
        curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);

        // CURLOPT_XFERINFOFUNCTION: 设置下载进度回调函数
        curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, Downloader::progress_callback);
        // CURLOPT_XFERINFODATA: 传递给进度回调的参数（可选，这里传nullptr）
        curl_easy_setopt(curl_, CURLOPT_XFERINFODATA, this);
        // CURLOPT_NOPROGRESS: 关闭旧版进度回调，必须设置为0才能启用新版
        curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L);  
    };


    void perform() {
        CURLcode res = curl_easy_perform(curl_);
        if (res != CURLE_OK) {
            has_error_ = true;
            error_msg_ = "download filed";
        }
        else {
            is_running_ = false;
        }
    }

    struct Progress {
        std::string filename;
        curl_off_t total_bytes;
        curl_off_t downloaded_bytes;
        bool is_running;
        bool has_error;
        std::string error_msg;
    };

    Progress getProgress() const {
        std::lock_guard<std::mutex> lock(mutex_);  // 线程安全读取
        return {
            outputFilename_,
            total_bytes_,
            downloaded_bytes_,
            is_running_,
            has_error_,
            error_msg_
        };
    }

private:
    // 回调函数：libcurl 下载数据时自动调用，将数据写入文件
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::ofstream *file) {
        size_t totalSize = size * nmemb;
        file->write(static_cast<char *>(contents), totalSize); // 写入文件
        return totalSize;                                      // 返回写入的字节数
    }

    // 进度条回调函数
    static int progress_callback(void *clientp,
                        curl_off_t dltotal,   // 总大小（bytes）
                        curl_off_t dlnow,     // 已下载（bytes）
                        curl_off_t ultotal, curl_off_t ulnow)
    {
        // clientp 就是 CURLOPT_XFERINFODATA 传入的指针（这里我们用来传文件名）
        Downloader *self = static_cast<Downloader*> (clientp);

        if (dltotal > 0) {
            self->total_bytes_ = dltotal; 
        }
        self->downloaded_bytes_ = dlnow;
        
        return 0; // 返回非0会中断下载
    }

private:
    std::string url_;
    std::string outputFilename_;
    std::ofstream file_;
    CURL *curl_;

    mutable std::mutex mutex_;
    curl_off_t total_bytes_ = 0;//总大小
    curl_off_t downloaded_bytes_ = 0;//已下载
    bool is_running_ = true;
    bool has_error_ = false;
    std::string error_msg_;

};

class DownloadManager {
public:
    void addTask(const std::string& url, const std::string& filename){
        tasks_.push_back(std::make_shared<Downloader>(url, filename));
    }

    void start() {
        for(auto& task : tasks_) {
            threads_.emplace_back([task]() {
                task->perform();
            });
        }
        printProgressLoop();
        
        for(auto& th : threads_) {
            if (th.joinable()) th.join();
        }
    }
private:
    void printProgressLoop() {
        while (hasActiveTasks()) {
            printProgressPanel();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        printProgressPanel();
        std::cout << "\n All downloads completed!" << std::endl;
    }

    bool hasActiveTasks() {
        for (auto& task : tasks_) {
            auto p = task->getProgress();
            if (p.is_running && !p.has_error){
                return true;
            }
        }
        return false;
    }

    void printProgressPanel() {
        static std::size_t previous_lines = 0;

        if (previous_lines > 0) {
            std::cout << "\033[" << previous_lines << "F\033[J"; // 移动光标到面板顶部并清空之后内容
        }

        std::ostringstream panel;

        panel << "==================================================\n";
        panel << "🚀 Download Manager (" << tasks_.size() << " tasks)\n";
        panel << "--------------------------------------------------\n";

        curl_off_t total_all = 0, downloaded_all = 0;

        for (auto& task : tasks_) {
            auto p = task->getProgress();

            if (p.total_bytes <= 0) continue;

            total_all += p.total_bytes;
            downloaded_all += p.downloaded_bytes;

            float progress = (float)p.downloaded_bytes / (float)p.total_bytes;
            int bar_width = 20;
            int pos = static_cast<int>(bar_width * progress);

            std::string bar;
            bar.reserve(bar_width);
            for (int i = 0; i < bar_width; ++i) {
                bar += (i < pos) ? "█" : "░";
            }

            double mb_now = p.downloaded_bytes / (1024.0 * 1024.0);
            double mb_total = p.total_bytes / (1024.0 * 1024.0);

            std::string status = p.has_error ? "❌ " + p.error_msg :
                               !p.is_running ? "✅ Done" : "";

            panel << std::left << std::setw(15) << p.filename
                  << " [" << bar << "] "
                  << std::right << std::setw(3) << static_cast<int>(progress * 100) << "% "
                  << "(" << std::fixed << std::setprecision(1)
                  << mb_now << "/" << mb_total << " MB) "
                  << status << "\n";
        }

        panel << "--------------------------------------------------\n";
        if (total_all > 0) {
            float overall_percent = (float)downloaded_all / (float)total_all;
            panel << "Overall: " << static_cast<int>(overall_percent * 100) << "%\n";
        }
        panel << "==================================================\n";

        const std::string panel_str = panel.str();
        previous_lines = static_cast<std::size_t>(std::count(panel_str.begin(), panel_str.end(), '\n'));

        std::cout << panel_str;
        std::cout.flush();
    }
private:
    std::vector<std::shared_ptr<Downloader>> tasks_;
    std::vector<std::thread> threads_;
};

int main()
{
    DownloadManager dm;

// 添加下载任务
    dm.addTask("https://dl.wenku8.com/down.php?type=txt&node=2&id=82",  "file1.bin");  // ~5MB

    dm.start();

    return 0;
}