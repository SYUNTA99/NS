@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -Command "$scriptPath = '%~dp0@build.ps1'; $scriptText = Get-Content -Raw -Encoding UTF8 $scriptPath; $scriptBlock = [scriptblock]::Create($scriptText); if ('%~1' -ne '') { $scriptBlock.Invoke('%~1') } else { $scriptBlock.Invoke() }"
exit /b %errorlevel%
