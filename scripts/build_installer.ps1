param(
    [string]$Version = "0.1.0",
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$root       = Split-Path -Parent $PSScriptRoot
$buildDir   = Join-Path $root 'build'
$build32Dir = Join-Path $root 'build32'
$distDir    = Join-Path $root 'dist'
$releaseDir = Join-Path $root 'release'
$stageDir   = Join-Path $releaseDir 'stage'
$portRoot   = Join-Path $releaseDir "LiteIME-$Version-win64-portable"
$nsiFile    = Join-Path $root 'installer\liteime.nsi'
$outExe     = Join-Path $releaseDir "LiteIME-Setup-$Version-win64.exe"
$outZip     = Join-Path $releaseDir "LiteIME-$Version-win64-portable.zip"
$sumFile    = Join-Path $releaseDir 'SHA256SUMS.txt'

if (-not $SkipBuild) {
    Write-Host "[1/4] 编译 64 位与 32 位目标..." -ForegroundColor Cyan
    cmake --build $buildDir -j 8
    if ($LASTEXITCODE -ne 0) { throw "64 位编译失败" }
    cmake --build $build32Dir -j 8
    if ($LASTEXITCODE -ne 0) { throw "32 位编译失败" }
}

$srcHost    = if (Test-Path (Join-Path $buildDir 'LiteIMEHost.exe'))   { Join-Path $buildDir 'LiteIMEHost.exe' }   else { Join-Path $distDir 'LiteIMEHost.exe' }
$srcDll64   = if (Test-Path (Join-Path $buildDir 'LiteIME64.dll'))     { Join-Path $buildDir 'LiteIME64.dll' }     else { Join-Path $distDir 'LiteIME64.dll' }
$srcDll32   = if (Test-Path (Join-Path $build32Dir 'LiteIME32.dll'))   { Join-Path $build32Dir 'LiteIME32.dll' }   else { Join-Path $distDir 'LiteIME32.dll' }
$srcComp    = if (Test-Path (Join-Path $buildDir 'dict_compiler.exe')) { Join-Path $buildDir 'dict_compiler.exe' } else { Join-Path $distDir 'dict_compiler.exe' }
$srcBigram  = if (Test-Path (Join-Path $root 'data\bigram.tsv'))       { Join-Path $root 'data\bigram.tsv' }       else { Join-Path $distDir 'bigram.tsv' }
$srcDataDir = Join-Path $distDir 'data'
$srcManual  = Join-Path $root 'docs\LiteIME_User_Manual.pdf'
$srcIcon    = Join-Path $root 'assets\liteime.ico'

foreach ($req in @($srcHost, $srcDll64, $srcDll32, $srcComp, $srcBigram, $srcManual, $srcIcon, (Join-Path $srcDataDir 'liteime.dict'))) {
    if (-not (Test-Path $req)) {
        throw "缺少打包必需文件: $req"
    }
}

Write-Host "[2/4] 准备干净的打包暂存目录..." -ForegroundColor Cyan
if (Test-Path $stageDir) { Remove-Item $stageDir -Recurse -Force }
if (Test-Path $portRoot) { Remove-Item $portRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stageDir 'data\packs') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stageDir 'docs') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stageDir 'assets') | Out-Null

Copy-Item $srcHost   (Join-Path $stageDir 'LiteIMEHost.exe') -Force
Copy-Item $srcDll64  (Join-Path $stageDir 'LiteIME64.dll') -Force
Copy-Item $srcDll32  (Join-Path $stageDir 'LiteIME32.dll') -Force
Copy-Item $srcComp   (Join-Path $stageDir 'dict_compiler.exe') -Force
Copy-Item $srcBigram (Join-Path $stageDir 'bigram.tsv') -Force
Copy-Item (Join-Path $srcDataDir 'liteime.dict') (Join-Path $stageDir 'data\liteime.dict') -Force
foreach ($pack in @('common_en.dict', 'computer.dict', 'ext_cn.dict', 'pro_fields.dict')) {
    $packPath = Join-Path $srcDataDir "packs\$pack"
    if (-not (Test-Path $packPath)) { throw "缺少扩展词库包: $packPath" }
    Copy-Item $packPath (Join-Path $stageDir "data\packs\$pack") -Force
}
Copy-Item $srcManual (Join-Path $stageDir 'docs\LiteIME_User_Manual.pdf') -Force
Copy-Item $srcIcon   (Join-Path $stageDir 'assets\liteime.ico') -Force

$makensisCmd = Get-Command makensis.exe -ErrorAction SilentlyContinue
$makensisExe = if ($makensisCmd) { $makensisCmd.Source } else { 'C:\Users\86180\AppData\Local\Programs\w64devkit\bin\makensis.exe' }
if (-not (Test-Path $makensisExe)) {
    throw "未找到 makensis.exe（NSIS 编译器）。"
}

Write-Host "[3/4] 使用 NSIS 编译安装包 ($outExe)..." -ForegroundColor Cyan
if (Test-Path $outExe) { Remove-Item $outExe -Force }
& $makensisExe /INPUTCHARSET UTF8 "/DAPP_VERSION=$Version" "/DSTAGE_DIR=$stageDir" "/DOUT_FILE=$outExe" "/DROOT_DIR=$root" $nsiFile
if ($LASTEXITCODE -ne 0) { throw "NSIS 编译安装包失败 (退出码: $LASTEXITCODE)" }

Write-Host "[4/4] 生成绿色便携包 ($outZip) 与 SHA256SUMS.txt..." -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path (Join-Path $portRoot 'dist\data\packs') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $portRoot 'scripts') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $portRoot 'docs') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $portRoot 'assets') | Out-Null
Copy-Item (Join-Path $stageDir 'LiteIMEHost.exe')    (Join-Path $portRoot 'dist\') -Force
Copy-Item (Join-Path $stageDir 'LiteIME64.dll')      (Join-Path $portRoot 'dist\') -Force
Copy-Item (Join-Path $stageDir 'LiteIME32.dll')      (Join-Path $portRoot 'dist\') -Force
Copy-Item (Join-Path $stageDir 'dict_compiler.exe')  (Join-Path $portRoot 'dist\') -Force
Copy-Item (Join-Path $stageDir 'bigram.tsv')         (Join-Path $portRoot 'dist\') -Force
Copy-Item (Join-Path $stageDir 'data\*')             (Join-Path $portRoot 'dist\data\') -Recurse -Force
Copy-Item (Join-Path $root 'scripts\install.ps1')    (Join-Path $portRoot 'scripts\') -Force
Copy-Item (Join-Path $root 'scripts\uninstall.ps1')  (Join-Path $portRoot 'scripts\') -Force
Copy-Item $srcManual                                 (Join-Path $portRoot 'docs\') -Force
Copy-Item $srcIcon                                   (Join-Path $portRoot 'assets\') -Force

if (Test-Path $outZip) { Remove-Item $outZip -Force }
Compress-Archive -Path "$portRoot\*" -DestinationPath $outZip -CompressionLevel Optimal

$lines = @()
foreach ($artifact in @($outExe, $outZip)) {
    $hash = (Get-FileHash $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
    $name = Split-Path $artifact -Leaf
    $lines += "$hash  $name"
}
[System.IO.File]::WriteAllText($sumFile, ($lines -join "`n") + "`n", (New-Object System.Text.UTF8Encoding($false)))

Remove-Item $stageDir -Recurse -Force
Remove-Item $portRoot -Recurse -Force

Write-Host "打包完成：" -ForegroundColor Green
Get-ChildItem $releaseDir | Select-Object Name, Length
Get-Content $sumFile
