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

:: PC を張り付かせないよう msbuild の並列数を 10 に制限する (物理 20 コア中 10、 残りは OS / 編集用)
set "MSBUILD_CPUS=10"

call "%~dp0_common.cmd" :init
if errorlevel 1 exit /b 1

call "%~dp0_common.cmd" :generate_project
if errorlevel 1 exit /b 1

echo.
echo %CONFIG% ビルド...
call "%~dp0_common.cmd" :setup_msbuild
if errorlevel 1 exit /b 1

:: clangd 用インデックス (compile_commands.json) を最新化 (失敗してもビルドは続行)
call "%~dp0_common.cmd" :gen_compile_commands
if errorlevel 1 echo [WARN] compile_commands.json の生成に失敗しましたが、ビルドを続行します

msbuild build\NS.sln /p:Configuration=%CONFIG% /p:Platform=x64 /m:%MSBUILD_CPUS% /v:minimal
if errorlevel 1 (
    echo [WARN] 並列ビルド失敗。 /m:1 で再試行します...
    msbuild build\NS.sln /p:Configuration=%CONFIG% /p:Platform=x64 /m:1 /v:minimal
    if errorlevel 1 (
        echo [ERROR] ビルド失敗
        exit /b 1
    )
)

echo [OK] ビルド成功
exit /b 0
