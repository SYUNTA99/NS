# rename_folders.ps1
# Rename the ASCII build folders to the Japanese submission names.
# The .cmd builds with ASCII names (Game/Source) to stay parser-safe, then
# calls this script for the final rename.
#
# Japanese names are built from code points so THIS FILE stays pure ASCII --
# that avoids any ambiguity in how PowerShell decodes the script bytes
# (Windows PowerShell reads BOM-less files as the system ANSI codepage).
param([Parameter(Mandatory = $true)][string]$Out)
$ErrorActionPreference = 'Stop'

# 実行環境  (U+5B9F U+884C U+74B0 U+5883)
$jpExec = [string]([char]0x5B9F + [char]0x884C + [char]0x74B0 + [char]0x5883)
# ソースコード  (U+30BD U+30FC U+30B9 U+30B3 U+30FC U+30C9)
$jpSrc = [string]([char]0x30BD + [char]0x30FC + [char]0x30B9 + [char]0x30B3 + [char]0x30FC + [char]0x30C9)

$map = @(
    @{ from = 'Game';   to = $jpExec },
    @{ from = 'Source'; to = $jpSrc  }
)

foreach ($m in $map) {
    $src = Join-Path $Out $m.from
    $dst = Join-Path $Out $m.to
    if (Test-Path -LiteralPath $dst) { Remove-Item -Recurse -Force -LiteralPath $dst }
    if (Test-Path -LiteralPath $src) { Rename-Item -LiteralPath $src -NewName $m.to }
}

Write-Output ("renamed: Game -> {0} , Source -> {1}" -f $jpExec, $jpSrc)
