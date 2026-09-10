Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinAPI {
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
}
"@
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$ErrorActionPreference = "Stop"
$root = "C:\Users\Lenovo\WorkBuddy\2026-09-07-15-05-15\WallpaperDesk"
$exe = "$root\build\WallDesk.exe"
$testImg = "$root\test_wallpaper.bmp"
$logDir = "$env:LOCALAPPDATA\WallDesk\WallDesk\logs"
$regPath = "HKCU:\Software\WallDesk\WallDesk"

# stop existing instance
Get-Process | Where-Object { $_.ProcessName -eq 'WallDesk' } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

# minimize blocker window
$mw = [WinAPI]::FindWindow($null, "微波源调试工具")
if ($mw -ne 0) { [WinAPI]::ShowWindowAsync($mw, 6) | Out-Null }

# clean old logs
Remove-Item "$logDir\*.log" -Force -ErrorAction SilentlyContinue

# preset registry
if (!(Test-Path $regPath)) { New-Item -Path $regPath -Force | Out-Null }
Set-ItemProperty -Path $regPath -Name "restoreLast" -Value 1 -Type DWord
Set-ItemProperty -Path $regPath -Name "lastPath" -Value $testImg -Type String
Set-ItemProperty -Path $regPath -Name "lastType" -Value 0 -Type DWord
Set-ItemProperty -Path $regPath -Name "fxBrightness" -Value 50 -Type DWord
Set-ItemProperty -Path $regPath -Name "fxContrast" -Value 20 -Type DWord
Set-ItemProperty -Path $regPath -Name "fxSaturation" -Value 30 -Type DWord
Set-ItemProperty -Path $regPath -Name "fxBlur" -Value 0 -Type DWord
Set-ItemProperty -Path $regPath -Name "fxVignette" -Value 20 -Type DWord
Set-ItemProperty -Path $regPath -Name "fxGray" -Value 0 -Type DWord
Set-ItemProperty -Path $regPath -Name "overlay" -Value 1 -Type DWord
Set-ItemProperty -Path $regPath -Name "overlayClock" -Value 1 -Type DWord
Set-ItemProperty -Path $regPath -Name "overlayCalendar" -Value 0 -Type DWord
Set-ItemProperty -Path $regPath -Name "overlayText" -Value 1 -Type DWord
Set-ItemProperty -Path $regPath -Name "overlayTextValue" -Value "WallDesk v4.6.0" -Type String
Set-ItemProperty -Path $regPath -Name "overlayPos" -Value 2 -Type DWord
Set-ItemProperty -Path $regPath -Name "overlayScreen" -Value 0 -Type DWord
Set-ItemProperty -Path $regPath -Name "overlaySize" -Value 24 -Type DWord
Set-ItemProperty -Path $regPath -Name "spectrum" -Value 1 -Type DWord
Set-ItemProperty -Path $regPath -Name "spectrumBands" -Value 32 -Type DWord
Set-ItemProperty -Path $regPath -Name "sceneEnabled" -Value 0 -Type DWord

# start WallDesk
$proc = Start-Process -FilePath $exe -WorkingDirectory "$root\build" -ArgumentList "--verbose" -PassThru
Write-Host "started PID: $($proc.Id)"
Start-Sleep -Seconds 8

# screenshot
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bitmap = New-Object System.Drawing.Bitmap($screen.Width, $screen.Height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($screen.Location, [System.Drawing.Point]::Empty, $screen.Size)
$shot = "$root\verify_desktop.png"
$bitmap.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose(); $bitmap.Dispose()
Write-Host "screenshot: $shot"

# read log
$log = Get-ChildItem $logDir -Filter *.log -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($log) {
    Write-Host "--- latest log ---"
    Get-Content $log.FullName -Tail 40 | ForEach-Object { Write-Host $_ }
} else {
    Write-Host "no log found"
}

# stop WallDesk
$proc | Stop-Process -Force -ErrorAction SilentlyContinue
Get-Process | Where-Object { $_.ProcessName -eq 'WallDesk' } | Stop-Process -Force -ErrorAction SilentlyContinue
Write-Host "done"
