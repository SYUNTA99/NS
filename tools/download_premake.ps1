# Premake5 Downloader
$version = "5.0.0-beta2"
$url = "https://github.com/premake/premake-core/releases/download/v$version/premake-$version-windows.zip"
$zipPath = "$PSScriptRoot\premake5.zip"
$exePath = "$PSScriptRoot\premake5.exe"

if (Test-Path $exePath) {
    Write-Host "premake5.exe already exists" -ForegroundColor Green
    exit 0
}

Write-Host "Downloading Premake5 v$version..." -ForegroundColor Cyan
try {
    Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing
    Write-Host "Extracting..." -ForegroundColor Cyan
    Expand-Archive -Path $zipPath -DestinationPath $PSScriptRoot -Force
    Remove-Item $zipPath -Force
    Write-Host "Done! premake5.exe is ready" -ForegroundColor Green
} catch {
    Write-Host "Download failed: $_" -ForegroundColor Red
    exit 1
}
