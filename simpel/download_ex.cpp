#include <iostream>      
#include <curl/curl.h>  
#include <fstream>       
#include <memory>       
#include <string>       
#include <thread>

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
            std::cerr << "Download failed: " << curl_easy_strerror(res) << std::endl;
        }
        else {
            std::cout << "Download completed" << std::endl;
            double time;
            res = curl_easy_getinfo(curl_, CURLINFO_TOTAL_TIME, &time);
            if (res == CURLE_OK) {
                std::cout << "Downloading total time:" << time << std::endl;
            }
        }
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
        const char *filename = self->outputFilename_.c_str();

        if (dltotal > 0) {
            // 计算百分比
            int width = 40;
            float progress = (float)dlnow / (float)dltotal;
            int pos = static_cast<int>(width * progress);

            // 转换为 KB 或 MB 显示
            double downloaded_mb = (double)dlnow / (1024.0 * 1024.0);
            double total_mb = (double)dltotal / (1024.0 * 1024.0);

            // 清除当前行并重新打印（\r 回到行首）
            std::cout << "\r";
            std::cout << "Downloading " << filename << ": [";
            for (int i = 0; i < width; ++i) {
                if (i < pos) std::cout << "█";
                else std::cout << "░";
            }
            std::cout << "] "
                    << int(progress * 100) << "% "
                    << "(" << downloaded_mb << "/" << total_mb << " MB) ";

            std::cout.flush();
        }
        // 更新上次状态
        self->g_last_downloaded_ = dlnow;

        return 0; // 返回非0会中断下载
    }

private:
    std::string url_;
    std::string outputFilename_;
    std::ofstream file_;
    CURL *curl_;

    curl_off_t g_last_downloaded_ = 0; 

};

int main()
{
    // 下载地址和保存文件名
    const std::string url = "https://zd.shinnku.top/file/shinnku/0/ons/%E6%9C%88%E5%A7%ACPLUS-DISC.zip";
    const std::string outputFilename = "file1";

    const std::string url2 = "https://zd.shinnku.top/file/shinnku/0/ons/%E6%9C%88%E5%A7%ACPLUS-DISC.zip";
    const std::string outputFilename2 = "file2";

    // Use a lambda to create and run the downloader in a thread
    std::thread t1([&url, &outputFilename]() {
        Downloader dloader(url, outputFilename);
        dloader.perform();
    });

    // std::thread t2([&url2, &outputFilename2]() {
    //     Downloader dloader2(url2, outputFilename2);
    //     dloader2.perform();
    // });

    t1.join();
    // t2.join();
    return 0;
}