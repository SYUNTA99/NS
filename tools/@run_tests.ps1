param(
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
[Console]::InputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

$scriptFilePath = $MyInvocation.MyCommand.Path
if ([string]::IsNullOrEmpty($scriptFilePath) -and (Get-Variable -Name scriptPath -ErrorAction SilentlyContinue)) {
    $scriptFilePath = (Get-Variable -Name scriptPath).Value
}

$toolsDir = Split-Path -Parent $scriptFilePath
$repoRoot = (Resolve-Path (Join-Path $toolsDir "..")).Path
$commonCmd = Join-Path $toolsDir "_common.cmd"

function Invoke-CmdCommand {
    param(
        [string]$CommandLine
    )

    $wrapped = "@echo off && $CommandLine"
    & cmd /c $wrapped | Out-Host
    return [int]$LASTEXITCODE
}

Set-Location $repoRoot

Write-Host "==================================="
Write-Host "テストビルド・実行 ($Config)"
Write-Host "==================================="

Write-Host ""
Write-Host "[1/3] プロジェクト生成中..."
$result = Invoke-CmdCommand "call `"$commonCmd`" :init && call `"$commonCmd`" :generate_project"
if ($result -ne 0) {
    exit $result
}

$buildCommand = "call `"$commonCmd`" :init && call `"$commonCmd`" :setup_msbuild && msbuild build\NS.sln /p:Configuration=$Config /p:Platform=x64 /m /v:minimal"
$retryBuildCommand = "call `"$commonCmd`" :init && call `"$commonCmd`" :setup_msbuild && msbuild build\NS.sln /p:Configuration=$Config /p:Platform=x64 /m:1 /v:minimal"

Write-Host ""
Write-Host "[2/3] テストビルド中..."
$result = Invoke-CmdCommand $buildCommand
if ($result -ne 0) {
    Write-Host "[WARN] /m ビルド失敗。/m:1 で再試行します..."
    $result = Invoke-CmdCommand $retryBuildCommand
    if ($result -ne 0) {
        Write-Host "[ERROR] ビルド失敗"
        exit $result
    }
}

Write-Host ""
Write-Host "[3/3] テスト実行中..."
Write-Host "==================================="

$testExe = Join-Path $repoRoot ("build\bin\{0}-windows-x86_64\tests\tests.exe" -f $Config)
if (-not (Test-Path $testExe)) {
    Write-Host "[ERROR] テスト実行ファイルが見つかりません: $testExe"
    exit 1
}

# Tests.exe の spdlog 出力は stderr 経由。 scriptBlock.Invoke() (cmd ラッパ経由)
# だと ErrorActionPreference=Stop が stderr 1 行ごとに発動して中断するため、
# テスト実行中は EAP を Continue にしてから戻す
$savedEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    & $testExe --gtest_color=yes 2>&1 | Out-Host
    $testResult = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $savedEAP
}

Write-Host ""
if ($testResult -eq 0) {
    Write-Host "==================================="
    Write-Host "[OK] 全テスト成功"
    Write-Host "==================================="
} else {
    Write-Host "==================================="
    Write-Host "[FAILED] テスト失敗"
    Write-Host "==================================="
}

exit $testResult
