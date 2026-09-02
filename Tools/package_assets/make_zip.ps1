# make_zip.ps1
# Zip the package folder for handover.
#
# Uses ZipFile::CreateFromDirectory with an explicit UTF-8 entryNameEncoding so
# the general purpose bit 11 is set. Compress-Archive does not set it, and the
# Japanese folder names (exec-env / source-code) then show up mojibake in
# Explorer. The top folder is kept inside the archive so extracting does not
# scatter files into the current directory.
#
# This file stays pure ASCII: Windows PowerShell reads BOM-less files as the
# system ANSI codepage, so a non-ASCII byte here would be ambiguous.
param(
    [Parameter(Mandatory = $true)][string]$Src,
    [Parameter(Mandatory = $true)][string]$Dst
)
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.IO.Compression.FileSystem

if (-not (Test-Path -LiteralPath $Src)) {
    Write-Error "source folder not found: $Src"
    exit 1
}

if (Test-Path -LiteralPath $Dst) {
    Remove-Item -Force -LiteralPath $Dst
}

[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $Src,
    $Dst,
    [System.IO.Compression.CompressionLevel]::Optimal,
    $true,
    [System.Text.Encoding]::UTF8)

$size = (Get-Item -LiteralPath $Dst).Length
Write-Output ("zipped: {0} ({1:N1} MB)" -f $Dst, ($size / 1MB))
