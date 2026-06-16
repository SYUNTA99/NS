@echo off
::============================================================================
:: @run_tests_only.cmd
:: ビルドせず、 既存のテスト実行ファイルをそのまま走らせる (PC への負荷を抑える)
::
:: 使用方法: tools\@run_tests_only.cmd [Config] [gtest_filter]
::   Config    省略時は Debug
::   filter    省略時は全テスト。 例: CameraBrainTest.*:ChunkIOTest.*
::
:: 事前に tools\@run_tests.cmd か tools\@build.cmd でビルドしておくこと。
::============================================================================
setlocal
chcp 65001 >nul

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
set "FILTER=%~2"

call "%~dp0_common.cmd" :init
if errorlevel 1 exit /b 1

set "TEST_EXE=build\bin\%CONFIG%-windows-x86_64\tests\tests.exe"
if not exist "%TEST_EXE%" (
    echo [ERROR] テスト実行ファイルが見つかりません: %TEST_EXE%
    echo         先に tools\@run_tests.cmd %CONFIG% でビルドしてください。
    exit /b 1
)

echo ===================================
if "%FILTER%"=="" (
    echo テスト実行 (%CONFIG%, build なし, 全件)
) else (
    echo テスト実行 (%CONFIG%, build なし, filter=%FILTER%)
)
echo ===================================

if "%FILTER%"=="" (
    "%TEST_EXE%" --gtest_color=yes
) else (
    "%TEST_EXE%" --gtest_color=yes --gtest_filter=%FILTER%
)
set "TEST_RESULT=%errorlevel%"

echo.
if "%TEST_RESULT%"=="0" (
    echo ===================================
    echo [OK] テスト成功
    echo ===================================
) else (
    echo ===================================
    echo [FAILED] テスト失敗
    echo ===================================
)

exit /b %TEST_RESULT%
