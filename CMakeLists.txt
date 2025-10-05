cmake_minimum_required(VERSION 3.16)
project(MultiDownloader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL REQUIRED)
find_package(fmt REQUIRED)

add_executable(mdown
    src/main.cpp
    src/download_manager.cpp
    src/multi_downloader.cpp
    src/detail/curl_utils.cpp
)

target_include_directories(mdown
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(mdown PRIVATE /W4 /permissive-)
else()
    target_compile_options(mdown PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TARGET CURL::libcurl)
    target_link_libraries(mdown PRIVATE CURL::libcurl fmt::fmt)
else()
    target_include_directories(mdown PRIVATE ${CURL_INCLUDE_DIRS})
    target_link_libraries(mdown PRIVATE ${CURL_LIBRARIES} fmt::fmt)
endif()
