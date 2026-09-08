cmake_minimum_required(VERSION 3.21)
project(WallDesk VERSION 4.4.0 LANGUAGES CXX)

option(WALLDESK_DEPLOY_QT "构建后自动运行 windeployqt 部署 Qt 运行时" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Windows 平台宏：精简 windows.h，避免 min/max 宏污染
if (WIN32)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    enable_language(RC) # 用于编译内嵌 VLC 运行时与版本/图标资源
endif()

# 自动识别 Qt6 / Qt5。Network 供单实例协作（QLocalServer/QLocalSocket）使用。
find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets Network REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets Network REQUIRED)

set(PROJECT_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/WallpaperEngine.cpp
    src/VlcPlayer.cpp
    src/DesktopWatcher.cpp
    src/VlcBundle.cpp
    src/AppTheme.cpp
    src/ThumbnailLoader.cpp
    src/FullscreenGuard.cpp
    src/AppPaths.cpp
    src/Logger.cpp
    src/SingleInstance.cpp
    src/MainWindow.h
    src/WallpaperEngine.h
    src/VlcPlayer.h
    src/DesktopWatcher.h
    src/VlcBundle.h
    src/AppTheme.h
    src/ThumbnailLoader.h
    src/FullscreenGuard.h
    src/AppPaths.h
    src/Logger.h
    src/SingleInstance.h
)

# 内嵌 VLC 运行时：由 tools/pack_vlc.py 生成，缺失时自动降级为外部搜索
set(WALLDESK_EMBED_VLC OFF)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/VlcBundle.bin"
    AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/VlcBundle.rc")
    set(WALLDESK_EMBED_VLC ON)
    list(APPEND PROJECT_SOURCES src/VlcBundle.rc)
endif()

# 版本与图标资源：版本号从 project() 注入，图标缺失时仍可编译
set(WALLDESK_VERSION_STR "${PROJECT_VERSION}")
set(WALLDESK_FILEVERSION
    "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
set(WALLDESK_DESCRIPTION "WallDesk - Windows image/video wallpaper tool")
set(WALLDESK_ICON_LINE "")
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico")
    set(WALLDESK_ICON_LINE
        "IDI_ICON1 ICON DISCARDABLE \"${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico\"")
    message(STATUS "应用图标：resources/app.ico")
else()
    message(STATUS "应用图标：未找到 resources/app.ico（运行 tools/make_icon.py 生成），"
                   "本次编译将不带图标")
endif()
configure_file(src/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
list(APPEND PROJECT_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/app.rc")

add_executable(WallDesk WIN32 ${PROJECT_SOURCES})

if (WALLDESK_EMBED_VLC)
    target_compile_definitions(WallDesk PRIVATE WALLDESK_EMBED_VLC)
    message(STATUS "内置 VLC 运行时：开（已链接 src/VlcBundle.bin）")
else()
    message(STATUS "内置 VLC 运行时：关（未找到打包产物，将回退到外部 libvlc.dll）")
endif()

target_compile_definitions(WallDesk PRIVATE WALLDESK_VERSION="${PROJECT_VERSION}")
target_link_libraries(WallDesk PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
                                       Qt${QT_VERSION_MAJOR}::Network)

if (WIN32)
    # 壁纸相关：SystemParametersInfo / EnumWindows / SetParent
    # 资源相关：FindResource / LoadResource（读取内嵌 VLC 运行时）
    # 注册表读取：RegOpenKeyEx（Logger 的系统信息探测）
    target_link_libraries(WallDesk PRIVATE user32 gdi32 shell32 advapi32)
endif()

# 编译完成后，把 Qt 依赖的 DLL 拷到 exe 目录。
# 默认关闭（每次构建都跑 windeployqt 很慢），需要时：cmake -DWALLDESK_DEPLOY_QT=ON
if (WIN32 AND WALLDESK_DEPLOY_QT)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_PREFIX_PATH}/bin")
    if (WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET WallDesk POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:WallDesk>"
            COMMENT "正在部署 Qt 运行时依赖..."
            VERBATIM
        )
    endif()
endif()
