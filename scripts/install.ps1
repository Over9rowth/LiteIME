$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "请使用『以管理员身份运行』的 PowerShell 执行此脚本以注册 TSF 系统级输入法。" -ForegroundColor Yellow
    exit 1
}

$root   = Split-Path -Parent $PSScriptRoot
$build  = Join-Path $root 'build'
$build32= Join-Path $root 'build32'
$dist   = Join-Path $root 'dist'
$dll64  = Join-Path $dist 'LiteIME64.dll'
$dll32  = Join-Path $dist 'LiteIME32.dll'
$tip    = "0804:{A9B4C21E-7D3A-4F8B-9C12-5E7A1D3B8C40}{B8C5D32F-8E4B-409C-AD23-6F8B2E4C9D51}"

if (-not (Test-Path $dll64)) {
    throw "未找到 $dll64，请先编译并打包 dist。"
}

# Stop existing host so LiteIMEHost.exe is unlocked
Get-Process -Name LiteIMEHost -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

# Clean up any previous renamed DLLs if unlocked; rename locked DLLs so build copies succeed without reboot
Get-ChildItem $dist -Filter "*.old.*" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
$buildHost  = Join-Path $build 'LiteIMEHost.exe'
$buildDll64 = Join-Path $build 'LiteIME64.dll'
$buildDll32 = Join-Path $build32 'LiteIME32.dll'
if ((Test-Path $buildHost) -and (Test-Path $buildDll64)) {
    try {
        Copy-Item $buildHost (Join-Path $dist "LiteIMEHost.exe") -Force
    } catch {
        Write-Warning "LiteIMEHost.exe 暂时无法覆盖: $_"
    }
    try {
        Copy-Item $buildDll64 $dll64 -Force
    } catch {
        $bak = "$dll64.old.$(Get-Date -Format 'yyyyMMddHHmmss')"
        Rename-Item $dll64 $bak -Force
        Copy-Item $buildDll64 $dll64 -Force
    }
}
if (Test-Path $buildDll32) {
    try {
        Copy-Item $buildDll32 $dll32 -Force
    } catch {
        $bak32 = "$dll32.old.$(Get-Date -Format 'yyyyMMddHHmmss')"
        if (Test-Path $dll32) { Rename-Item $dll32 $bak32 -Force }
        Copy-Item $buildDll32 $dll32 -Force
    }
}

$bigramSrc = Join-Path $root 'data\bigram.tsv'
if (Test-Path $bigramSrc) {
    Copy-Item $bigramSrc (Join-Path $dist 'bigram.tsv') -Force
}

# Grant ALL APPLICATION PACKAGES (UWP / Edge / Modern Apps) RX access to dist
icacls $dist /grant "*S-1-15-2-1:(OI)(CI)(RX)" /T | Out-Null

# Register 64-bit TSF COM Server + Profile
$proc = Start-Process -FilePath "regsvr32.exe" -ArgumentList "/s `"$dll64`"" -Wait -PassThru
if ($proc.ExitCode -ne 0) {
    throw "regsvr32 (64位) 注册失败，退出码: $($proc.ExitCode)"
}

# Register 32-bit WOW64 TSF COM Server if present
$regsvr32Wow64 = Join-Path $env:SystemRoot 'SysWOW64\regsvr32.exe'
if ((Test-Path $dll32) -and (Test-Path $regsvr32Wow64)) {
    $proc32 = Start-Process -FilePath $regsvr32Wow64 -ArgumentList "/s `"$dll32`"" -Wait -PassThru
    if ($proc32.ExitCode -ne 0) {
        Write-Warning "regsvr32 (32位 WOW64) 注册返回退出码: $($proc32.ExitCode)"
    }
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
Write-Host "（首次在记事本等程序打字时会自动拉起候选窗宿主进程，或在普通权限终端运行 $(Join-Path $dist 'LiteIMEHost.exe')）" -ForegroundColor Cyan
