# mdown · 多线程下载器

一个基于 libcurl 的多线程命令行下载工具。默认使用 8 个线程，将下载内容保存到 `~/download` 目录，适合在 Linux 终端中快速批量拉取文件。

## 功能特性

- **多线程下载**：默认 8 线程，可在源码中调整。
- **自动创建目录**：首次运行会在用户主目录下创建 `~/download`。
- **实时进度面板**：显示每个任务的下载进度、速度条和最终状态。
- **错误提示**：网络或文件错误会在进度面板和退出码中体现。

## 环境依赖

- C++17 编译器（g++ / clang++）
- CMake ≥ 3.16
- `libcurl` 开发包
- `fmt` 库

在 Debian/Ubuntu 上可以使用：

```bash
sudo apt install build-essential cmake libcurl4-openssl-dev libfmt-dev
```

## 构建步骤

```bash
cd download/mutl
cmake -S . -B build
cmake --build build --target mdown
```

构建完成后，二进制位于 `build/mdown`。

## 使用方式

```bash
./build/mdown [-d <directory>] "<url1>" <file1> ["<url2>" <file2> ...] ```

- `-d <directory>`：可选，自定义输出目录（会自动创建）。
- 默认不指定目录时，文件保存到 `~/download`。
- URL 中如果包含 `&`，请务必加引号或对 `&` 进行转义。
- 目标文件名无需写绝对路径，程序会自动拼接到目标目录。
- 成功下载的任务会显示 `✅ Done`，发生错误会显示 `❌` 及错误原因。

示例：

```bash
./build/mdown "https://example.com/archive.zip" archive.zip
./build/mdown -d /tmp/mydir "https://example.com/video.mp4" video.mp4
```

## 常见问题

- **提示用法而未下载**：检查参数个数是否为偶数，或 URL 是否缺少引号。
- **依赖缺失**：确认已安装 `libcurl` 与 `fmt` 开发包。
- **权限问题**：若无法写入 `~/download`，请检查目录权限，或修改源码中的默认路径。# Multi-threaded-Downloader
