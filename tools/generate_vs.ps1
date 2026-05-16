$ErrorActionPreference = "Stop"
$guid = [guid]::NewGuid().ToString()
$tempPath = "$env:TEMP\$guid"
$projectRoot = Split-Path -Parent $PSScriptRoot

# Create junction
cmd /c "mklink /j `"$tempPath`" `"$projectRoot`""

try {
    Push-Location $tempPath
    & ".\tools\premake5.exe" vs2022
    if ($LASTEXITCODE -ne 0) {
        throw "Premake5 failed"
    }
}
finally {
    Pop-Location
    cmd /c "rmdir `"$tempPath`""
}
