Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinAPI {
    public static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);
    public static readonly IntPtr HWND_NOTOPMOST = new IntPtr(-2);
    public const uint SWP_NOMOVE = 0x0002;
    public const uint SWP_NOSIZE = 0x0001;
    public const uint SWP_SHOWWINDOW = 0x0040;
    public const uint SWP_NOACTIVATE = 0x0010;
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
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

function Stop-WallDesk {
    Get-Process | Where-Object { $_.ProcessName -eq 'WallDesk' } | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
}

function Clear-Logs {
    Remove-Item "$logDir\*.log" -Force -ErrorAction SilentlyContinue
}

function Minimize-Blocker {
    $mw = [WinAPI]::FindWindow($null, "微波源调试工具")
    if ($mw -ne 0) { [WinAPI]::ShowWindowAsync($mw, 6) | Out-Null }
}

function Set-Registry {
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
}

function Capture-Screen($outPath) {
    $screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bitmap = New-Object System.Drawing.Bitmap($screen.Width, $screen.Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($screen.Location, [System.Drawing.Point]::Empty, $screen.Size)
    $bitmap.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    Write-Host "screenshot: $outPath"
}

function Start-WallDeskAndCapture($page, $outPath) {
    reg add "HKCU\Software\WallDesk\WallDesk" /v winPage /t REG_DWORD /d $page /f | Out-Null
    $actual = (reg query "HKCU\Software\WallDesk\WallDesk" /v winPage 2>$null) -join " "
    Write-Host "set winPage=$page  reg=$actual"
    $proc = Start-Process -FilePath $exe -WorkingDirectory "$root\build" -ArgumentList "--verbose" -PassThru
    Write-Host "started PID: $($proc.Id) for page $page"
    Start-Sleep -Seconds 8

    $hwnd = $proc.MainWindowHandle
    if ($hwnd -ne 0) {
        [WinAPI]::ShowWindow($hwnd, 9) | Out-Null
        Start-Sleep -Milliseconds 200
        [WinAPI]::SetWindowPos($hwnd, [WinAPI]::HWND_TOPMOST, 0, 0, 0, 0,
                               [WinAPI]::SWP_NOMOVE -bor [WinAPI]::SWP_NOSIZE -bor [WinAPI]::SWP_SHOWWINDOW) | Out-Null
        [WinAPI]::SetForegroundWindow($hwnd) | Out-Null
        Start-Sleep -Milliseconds 500
    }
    Capture-Screen $outPath
    if ($hwnd -ne 0) {
        [WinAPI]::SetWindowPos($hwnd, [WinAPI]::HWND_NOTOPMOST, 0, 0, 0, 0,
                               [WinAPI]::SWP_NOMOVE -bor [WinAPI]::SWP_NOSIZE) | Out-Null
    }
    $proc | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
}

Stop-WallDesk
Minimize-Blocker
Clear-Logs
Set-Registry

# Page 0 = library (with merged tool row)
Start-WallDeskAndCapture 0 "$root\verify_desktop.png"

# Page 1 = settings (show effects reset button near bottom)
Start-WallDeskAndCapture 1 "$root\verify_settings.png"

# Page 2 = monitors (show unique monitor labels)
Start-WallDeskAndCapture 2 "$root\verify_monitors.png"

# read log
$log = Get-ChildItem $logDir -Filter *.log -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($log) {
    Write-Host "--- latest log ---"
    Get-Content $log.FullName -Tail 40 | ForEach-Object { Write-Host $_ }
} else {
    Write-Host "no log found"
}

Stop-WallDesk
Write-Host "done"
