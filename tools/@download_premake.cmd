@echo off
::============================================================================
:: @download_premake.cmd
:: Fetch Premake5 from GitHub Releases into tools\premake5\premake5.exe.
::
:: Usage: tools\@download_premake.cmd
::
:: Requires: curl.exe / tar.exe (bundled since Windows 10 1803)
::
:: NOTE: this header must be ASCII. It is parsed before chcp 65001 (line below)
::       takes effect, and cmd.exe misparses UTF-8 multibyte here
::       (a trailing byte eats the CR and a comment fragment gets executed).
::============================================================================
setlocal
chcp 65001 >nul

set "EXE=%~dp0premake5\premake5.exe"
if exist "%EXE%" (
    echo [OK] premake5.exe は既に存在します
    pause
    exit /b 0
)

set "VERSION=5.0.0-beta2"
set "URL=https://github.com/premake/premake-core/releases/download/v%VERSION%/premake-%VERSION%-windows.zip"
set "ZIP=%~dp0premake5.zip"

echo Premake5 v%VERSION% をダウンロード中...
curl.exe -L -f -o "%ZIP%" "%URL%"
if errorlevel 1 (
    echo [ERROR] ダウンロード失敗 (URL: %URL%)
    pause
    exit /b 1
)

echo 展開中...
if not exist "%~dp0premake5" mkdir "%~dp0premake5"
tar.exe -xf "%ZIP%" -C "%~dp0premake5"
if errorlevel 1 (
    echo [ERROR] 展開失敗
    del "%ZIP%" 2>nul
    pause
    exit /b 1
)

del "%ZIP%"
echo [OK] premake5.exe 準備完了
pause
exit /b 0
