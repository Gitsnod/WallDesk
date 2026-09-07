@echo off
chcp 65001 >nul
setlocal

rem ============ 按你的实际安装路径修改下面几行 ============
set QT_DIR=C:\Qt\6.11.2\mingw_64
set MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin
set VLC_DIR=C:\Program Files\VideoLAN\VLC
rem ========================================================

rem Python：优先用系统 PATH 中的 python，其次尝试 py 启动器
set PYTHON=
where python >nul 2>nul && set PYTHON=python
if not defined PYTHON (
    where py >nul 2>nul && set PYTHON=py
)

set PATH=%MINGW_BIN%;%QT_DIR%\bin;%PATH%

echo [1/5] 检查应用图标...
if exist resources\app.ico (
    echo   已存在 resources\app.ico，跳过生成。
) else (
    if exist "%PYTHON%" (
        "%PYTHON%" tools\make_icon.py
    ) else (
        echo   未找到 PYTHON，本次编译将不带图标。
    )
)

echo [2/5] 打包内置 VLC 运行时（可选）...
if exist "%VLC_DIR%\libvlc.dll" (
    if exist "%PYTHON%" (
        "%PYTHON%" tools\pack_vlc.py --vlc "%VLC_DIR%"
        if errorlevel 1 (
            echo   打包失败，本次将构建为「不内嵌」版本。
        ) else (
            echo   已生成 src\VlcBundle.bin，构建时会嵌入 exe。
        )
    ) else (
        echo   未找到 PYTHON，跳过内嵌打包。请在脚本顶部指定 python.exe 路径。
    )
) else (
    echo   未找到 VLC_DIR 中的 libvlc.dll，跳过内嵌打包。
    echo   程序仍可编译运行，只是视频功能需要手动提供 libvlc.dll。
)

echo [3/5] 配置 CMake...
cmake -S . -B build -G "Ninja" -DCMAKE_PREFIX_PATH=%QT_DIR%
if errorlevel 1 (
    echo 配置失败：请检查 QT_DIR 是否指向正确的 Qt 套件目录。
    goto :fail
)

echo [4/5] 编译...
cmake --build build
if errorlevel 1 (
    echo 编译失败。
    goto :fail
)

echo [5/5] 部署 Qt 运行时...
if exist "%QT_DIR%\bin\windeployqt.exe" (
    "%QT_DIR%\bin\windeployqt.exe" build\WallDesk.exe
) else (
    echo 未找到 windeployqt，跳过 Qt 运行时部署。
)

echo.
echo 构建完成：build\WallDesk.exe
echo   - 已内嵌 VLC 运行时时：单文件即可使用视频壁纸，无需安装 VLC
echo   - 未内嵌时：把 libvlc.dll、libvlccore.dll、plugins 放到 exe 同级目录

if /i "%1"=="release" (
    echo.
    echo ============ 打发布包 ============
    "%PYTHON%" tools\make_release.py --qt-bin "%QT_DIR%\bin"
    if errorlevel 1 (
        echo 发布打包失败。
        goto :fail
    )
)
goto :end

:fail
echo 构建中断。

:end
endlocal
pause
