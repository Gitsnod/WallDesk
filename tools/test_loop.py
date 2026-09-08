#!/usr/bin/env python3
"""观察视频循环行为：启动后等待一段时间，分析日志中的循环间隔。"""
import os
import re
import subprocess
import time

EXE = r'D:\Qt\WallpaperDesk\build\WallDesk.exe'
LOG = os.path.join(os.environ.get('USERPROFILE', r'C:\Users\Lenovo'),
                   r'AppData\Local\WallDesk\WallDesk\logs\WallDesk.log')

subprocess.run(['taskkill', '/F', '/IM', 'WallDesk.exe'], capture_output=True)
time.sleep(1)

proc = subprocess.Popen([EXE], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print(f'启动 WallDesk pid={proc.pid}，观察 100 秒…')

markers = []
deadline = time.time() + 100
start_size = os.path.getsize(LOG) if os.path.exists(LOG) else 0

while time.time() < deadline:
    time.sleep(5)
    if not os.path.exists(LOG):
        continue
    with open(LOG, 'r', encoding='utf-8', errors='ignore') as f:
        f.seek(start_size)
        for line in f:
            if '循环' in line or '重播' in line or '异常' in line or '失败' in line:
                m = re.match(r'(\S+ \S+)', line)
                markers.append(line.strip())

proc.terminate()
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()

print('---- 循环/异常相关日志 ----')
for line in markers:
    print(' ', line)
if not markers:
    print('  （无）')
