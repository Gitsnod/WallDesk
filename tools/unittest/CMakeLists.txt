cmake_minimum_required(VERSION 3.16)
project(WallDeskUnitTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# 用相对路径定位仓库根目录。此前写死了 D:/Qt/WallpaperDesk，
# 结果是「在 C 盘那份工作副本里跑测试，验的却是 D 盘的代码」。
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SRC "${REPO_ROOT}/src")

add_executable(test_v47_units
    "${REPO_ROOT}/tools/test_v47_units.cpp"
    "${SRC}/MediaInfo.cpp"
    "${SRC}/PresetManager.cpp"
    "${SRC}/AppPaths.cpp"
    "${SRC}/Logger.cpp"
    "${SRC}/OnlineSources.cpp"
)
target_include_directories(test_v47_units PRIVATE "${SRC}")
# OnlineSources 用到 QNetworkAccessManager，链接时必须有 Qt Network
target_link_libraries(test_v47_units PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
