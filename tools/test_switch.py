#!/usr/bin/env python3
"""测试图片/视频壁纸来回切换是否卡顿或无响应。

每个 --next 都会触发「下一张」：在库中循环切换图片与视频。
由于单实例机制，新进程会把指令转发给已有实例后自行退出。
"""
import os
import subprocess
import time

EXE = r"D:\Qt\WallpaperDesk\build\WallDesk.exe"
LOG = os.path.join(os.environ.get("USERPROFILE", r"C:\Users\Lenovo"),
                   r"AppData\Local\WallDesk\WallDesk\logs\WallDesk.log")

subprocess.run(["taskkill", "/F", "/IM", "WallDesk.exe"], capture_output=True)
time.sleep(1)

proc = subprocess.Popen([EXE], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print(f"启动 WallDesk pid={proc.pid}")
time.sleep(6)

start_size = os.path.getsize(LOG) if os.path.exists(LOG) else 0

for i, label in enumerate(["视频/图片切换 #1", "视频/图片切换 #2", "视频/图片切换 #3"], 1):
    subprocess.run([EXE, "--next"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"[{i}/3] 发送 --next ({label})")
    time.sleep(10)

# 检查日志中是否有异常/无响应相关记录
markers = []
if os.path.exists(LOG):
    with open(LOG, "r", encoding="utf-8", errors="ignore") as f:
        f.seek(start_size)
        for line in f:
            if any(k in line for k in ["异常", "失败", "卡住", "无响应", "停滞", "Error"]):
                markers.append(line.strip())

if markers:
    print("---- 异常日志 ----")
    for line in markers:
        print(" ", line)
else:
    print("切换过程中未出现异常日志")

proc.terminate()
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()
print("测试完成")
