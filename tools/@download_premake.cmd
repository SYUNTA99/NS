@echo off
::============================================================================
:: @download_premake.cmd
:: Premake5 を GitHub Releases から取得して tools\premake5.exe に配置
::
:: 使用方法: tools\@download_premake.cmd
::
:: 依存: curl.exe / tar.exe (Windows 10 1803 以降標準搭載)
::============================================================================
setlocal
chcp 65001 >nul

set "EXE=%~dp0premake5.exe"
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
tar.exe -xf "%ZIP%" -C "%~dp0"
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
