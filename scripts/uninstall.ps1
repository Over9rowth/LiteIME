$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "请使用『以管理员身份运行』的 PowerShell 执行此卸载脚本。" -ForegroundColor Yellow
    exit 1
}

$root = Split-Path -Parent $PSScriptRoot
$dll  = Join-Path $root 'dist\LiteIME64.dll'
$tip  = "0804:{A9B4C21E-7D3A-4F8B-9C12-5E7A1D3B8C40}{B8C5D32F-8E4B-409C-AD23-6F8B2E4C9D51}"

Get-Process -Name LiteIMEHost -ErrorAction SilentlyContinue | Stop-Process -Force

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class InputApiUninstall {
    [DllImport("input.dll", CharSet = CharSet.Unicode)]
    public static extern bool InstallLayoutOrTip(string psz, uint dwFlags);
}
"@
[void][InputApiUninstall]::InstallLayoutOrTip($tip, 1) # ILOT_UNINSTALL = 1

$langs = Get-WinUserLanguageList
$zh = $langs | Where-Object { $_.LanguageTag -like "zh*" } | Select-Object -First 1
if ($zh -and ($zh.InputMethodTips -contains $tip)) {
    [void]$zh.InputMethodTips.Remove($tip)
    Set-WinUserLanguageList $langs -Force
}

if (Test-Path $dll) {
    $proc = Start-Process -FilePath "regsvr32.exe" -ArgumentList "/s /u `"$dll`"" -Wait -PassThru
    if ($proc.ExitCode -ne 0) {
        Write-Warning "regsvr32 卸载返回退出码: $($proc.ExitCode)（若 DLL 仍被其他应用加载，注销或重启后可清理）"
    }
}

Write-Host "LiteIME 已注销并移除。" -ForegroundColor Green
