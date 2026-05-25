@echo off
::============================================================================
:: @build.cmd
:: Premake5 で VS ソリューションを生成 → MSBuild で指定構成をビルド
::
:: 使用方法: tools\@build.cmd [Debug|Development|GameDebug|GameRelease]
::   省略時は Debug
::============================================================================
setlocal
chcp 65001 >nul

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"

call "%~dp0_common.cmd" :init
if errorlevel 1 exit /b 1

call "%~dp0_common.cmd" :generate_project
if errorlevel 1 exit /b 1

echo.
echo %CONFIG% ビルド...
call "%~dp0_common.cmd" :setup_msbuild
if errorlevel 1 exit /b 1

msbuild build\NS.sln /p:Configuration=%CONFIG% /p:Platform=x64 /m /v:minimal
if errorlevel 1 (
    echo [WARN] /m ビルド失敗。 /m:1 で再試行します...
    msbuild build\NS.sln /p:Configuration=%CONFIG% /p:Platform=x64 /m:1 /v:minimal
    if errorlevel 1 (
        echo [ERROR] ビルド失敗
        exit /b 1
    )
)

echo [OK] ビルド成功
exit /b 0
