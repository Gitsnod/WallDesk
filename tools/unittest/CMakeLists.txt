cmake_minimum_required(VERSION 3.16)
project(WallDeskUnitTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
set(SRC "D:/Qt/WallpaperDesk/src")

add_executable(test_v47_units
    "D:/Qt/WallpaperDesk/tools/test_v47_units.cpp"
    "${SRC}/MediaInfo.cpp"
    "${SRC}/PresetManager.cpp"
    "${SRC}/AppPaths.cpp"
    "${SRC}/Logger.cpp"
)
target_include_directories(test_v47_units PRIVATE "${SRC}")
target_link_libraries(test_v47_units PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
