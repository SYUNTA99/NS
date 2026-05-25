@echo off
::============================================================================
:: @run_tests.cmd
:: テストをビルドして実行
::
:: 使用方法: tools\@run_tests.cmd [Debug|Development|GameDebug|GameRelease]
::   省略時は Debug
::============================================================================
setlocal
chcp 65001 >nul

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"

echo ===================================
echo テストビルド・実行 (%CONFIG%)
echo ===================================
echo.

call "%~dp0_common.cmd" :init
if errorlevel 1 exit /b 1

echo [1/3] プロジェクト生成中...
call "%~dp0_common.cmd" :generate_project
if errorlevel 1 exit /b 1

echo.
echo [2/3] テストビルド中...
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

echo.
echo [3/3] テスト実行中...
echo ===================================

set "TEST_EXE=build\bin\%CONFIG%-windows-x86_64\tests\tests.exe"
if not exist "%TEST_EXE%" (
    echo [ERROR] テスト実行ファイルが見つかりません: %TEST_EXE%
    exit /b 1
)

"%TEST_EXE%" --gtest_color=yes
set "TEST_RESULT=%errorlevel%"

echo.
if "%TEST_RESULT%"=="0" (
    echo ===================================
    echo [OK] 全テスト成功
    echo ===================================
) else (
    echo ===================================
    echo [FAILED] テスト失敗
    echo ===================================
)

exit /b %TEST_RESULT%
