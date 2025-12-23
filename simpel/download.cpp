#include <iostream>      // 标准输入输出流
#include <curl/curl.h>   // libcurl 库头文件
#include <fstream>       // 文件流操作


// 全局变量用于记录上一次时间与速度计算
static time_t g_start_time_;
static size_t g_last_downloaded_;

class Downloader
{
public:
    Downloader(const char *url, const char *outfile) : url_(url), outputFilename_(outfile), file_(outputFilename_, std::ios::binary){
        curl_global_init(CURL_GLOBAL_ALL);

        curl_ = curl_easy_init();
        if (!curl_) {
            std::cerr << "Failed to initialize curl" << std::endl;
            return;
        }

        if (!file_) {
            std::cerr << "Cannot open file for writing" << std::endl; 
            curl_easy_cleanup(curl_);
            return;
        }
        // 设置 curl 选项
        // CURLOPT_URL: 要下载的目标 URL 地址
        curl_easy_setopt(curl_, CURLOPT_URL, url_);
        // CURLOPT_WRITEFUNCTION: 下载数据到本地时调用的回调函数（WriteCallback）
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, Downloader::WriteCallback);
        // CURLOPT_WRITEDATA: 传递给回调函数的第四个参数（这里是输出文件流指针 &file）
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &file_);
        // CURLOPT_FOLLOWLOCATION: 是否自动跟随 HTTP 3xx 重定向（1L 表示开启）
        curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
    };
    
    ~Downloader() {
        file_.close();
        curl_easy_cleanup(curl_);
        curl_global_cleanup();
    }

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
                printf("Downloding toltal time: %.lf\n", time);
            }
        }
    }

    void process() {
        g_last_downloaded_ = 0;
        g_start_time_ = 0;

        // CURLOPT_XFERINFOFUNCTION: 设置下载进度回调函数
        curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, Downloader::progress_callback);
        // CURLOPT_XFERINFODATA: 传递给进度回调的参数（可选，这里传nullptr）
        curl_easy_setopt(curl_, CURLOPT_XFERINFODATA, (void*)outputFilename_);
        // CURLOPT_NOPROGRESS: 关闭旧版进度回调，必须设置为0才能启用新版
        curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L);   
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
                        curl_off_t ultotal,
                        curl_off_t ulnow)
    {
        // clientp 就是 CURLOPT_XFERINFODATA 传入的指针（这里我们用来传文件名）
        const char *filename = static_cast<const char*>(clientp);

        // 初始化开始时间
        if (g_start_time_ == 0) {
            g_start_time_ = time(nullptr);
            g_last_downloaded_ = 0;
            return 0;
        }

        double speed = 0.0;
        time_t now = time(nullptr);
        time_t elapsed = now - g_start_time_;

        if (elapsed > 0) {
            speed = static_cast<double>((dlnow - g_last_downloaded_) / elapsed); // bytes per second
        }

        if (dltotal > 0) {
            // 计算百分比
            int width = 40;
            float progress = (float)dlnow / (float)dltotal;
            int pos = width * (int)progress;

            // 转换为 KB 或 MB 显示
            double downloaded_mb = (double)dlnow / (1024.0 * 1024.0);
            double total_mb = (double)dltotal / (1024.0 * 1024.0);
            double speed_mbps = speed / (1024.0 * 1024.0); // MB/s

            // 清除当前行并重新打印（\r 回到行首）
            std::cout << "\r";
            std::cout << "Downloading " << filename << ": [";
            for (int i = 0; i < width; ++i) {
                if (i < pos) std::cout << "█";
                else std::cout << "░";
            }
            std::cout << "] "
                    << int(progress * 100) << "% "
                    << "(" << downloaded_mb << "/" << total_mb << " MB) "
                    << speed_mbps << " MB/s";

            std::cout.flush();
        }

        // 更新上次状态
        g_last_downloaded_ = dlnow;

        return 0; // 返回非0会中断下载
    }

private:
    const char *url_;
    const char *outputFilename_;
    CURL *curl_;
    std::ofstream file_;


};
int main()
{
    // 下载地址和保存文件名
    const char *url = "https://zd.shinnku.top/file/shinnku/0/ons/%E6%9C%88%E5%A7%ACPLUS-DISC.zip";
    const char *outputFilename = "downloaded_file.txt";

    Downloader dloader(url, outputFilename);
    
    dloader.process();
    dloader.perform();
    
    return 0;
}