$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "请使用『以管理员身份运行』的 PowerShell 执行此脚本以注册 TSF 系统级输入法。" -ForegroundColor Yellow
    exit 1
}

$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist'
$dll  = Join-Path $dist 'LiteIME64.dll'
$tip  = "0804:{A9B4C21E-7D3A-4F8B-9C12-5E7A1D3B8C40}{B8C5D32F-8E4B-409C-AD23-6F8B2E4C9D51}"

if (-not (Test-Path $dll)) {
    throw "未找到 $dll，请先编译并打包 dist。"
}

# Stop existing host so LiteIMEHost.exe is unlocked
Get-Process -Name LiteIMEHost -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

# Clean up any previous renamed DLLs if unlocked; rename locked DLLs so build copies succeed without reboot
Get-ChildItem $dist -Filter "LiteIME64.dll.old.*" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
if ((Test-Path "D:\LiteIME\build\LiteIMEHost.exe") -and (Test-Path "D:\LiteIME\build\LiteIME64.dll")) {
    try {
        Copy-Item "D:\LiteIME\build\LiteIMEHost.exe" (Join-Path $dist "LiteIMEHost.exe") -Force
    } catch {
        Write-Warning "LiteIMEHost.exe 暂时无法覆盖: $_"
    }
    try {
        Copy-Item "D:\LiteIME\build\LiteIME64.dll" $dll -Force
    } catch {
        $bak = "$dll.old.$(Get-Date -Format 'yyyyMMddHHmmss')"
        Rename-Item $dll $bak -Force
        Copy-Item "D:\LiteIME\build\LiteIME64.dll" $dll -Force
    }
}

# Grant ALL APPLICATION PACKAGES (UWP / Edge / Modern Apps) RX access to dist
icacls $dist /grant "*S-1-15-2-1:(OI)(CI)(RX)" /T | Out-Null

# Register TSF COM Server + Profile
$proc = Start-Process -FilePath "regsvr32.exe" -ArgumentList "/s `"$dll`"" -Wait -PassThru
if ($proc.ExitCode -ne 0) {
    throw "regsvr32 注册失败，退出码: $($proc.ExitCode)"
}

# Enable TIP in Windows 10/11 user profile immediately (so Win + Space shows LiteIME right away)
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class InputApi {
    [DllImport("input.dll", CharSet = CharSet.Unicode)]
    public static extern bool InstallLayoutOrTip(string psz, uint dwFlags);
}
"@
[void][InputApi]::InstallLayoutOrTip($tip, 0)

$langs = Get-WinUserLanguageList
$zh = $langs | Where-Object { $_.LanguageTag -like "zh*" } | Select-Object -First 1
if ($zh -and -not ($zh.InputMethodTips -contains $tip)) {
    $zh.InputMethodTips.Add($tip)
    Set-WinUserLanguageList $langs -Force
}

Write-Host "LiteIME 注册并启用成功！按 Win + Space 即可切换到『极简双语输入法 (LiteIME)』。" -ForegroundColor Green
Write-Host "（首次在记事本等程序打字时会自动拉起候选窗宿主进程，或在普通权限终端运行 D:\LiteIME\dist\LiteIMEHost.exe）" -ForegroundColor Cyan
