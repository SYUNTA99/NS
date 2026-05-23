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

$result = Invoke-CmdCommand "call `"$commonCmd`" :init && call `"$commonCmd`" :generate_project"
if ($result -ne 0) {
    exit $result
}

Write-Host "$Config ビルド..."

$buildCommand = "call `"$commonCmd`" :init && call `"$commonCmd`" :setup_msbuild && msbuild build\NS.sln /p:Configuration=$Config /p:Platform=x64 /m /v:minimal"
$retryBuildCommand = "call `"$commonCmd`" :init && call `"$commonCmd`" :setup_msbuild && msbuild build\NS.sln /p:Configuration=$Config /p:Platform=x64 /m:1 /v:minimal"

$result = Invoke-CmdCommand $buildCommand
if ($result -ne 0) {
    Write-Host "[WARN] /m ビルド失敗。/m:1 で再試行します..."
    $result = Invoke-CmdCommand $retryBuildCommand
    if ($result -ne 0) {
        Write-Host "[ERROR] ビルド失敗"
        exit $result
    }
}

Write-Host "[OK] ビルド成功"
exit 0
