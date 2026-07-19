@echo off
::============================================================================
:: @run_tests_only.cmd
:: Run the existing tests.exe as-is without building (keeps the PC load low).
::
:: Usage: tools\@run_tests_only.cmd [Config] [gtest_filter]
::   Config    defaults to Debug when omitted
::   filter    defaults to all tests. e.g. CameraBrainTest.*:ChunkIOTest.*
::
:: Build first with tools\@run_tests.cmd or tools\@build.cmd.
::
:: NOTE: this header must be ASCII. It is parsed before chcp 65001 (line below)
::       takes effect, and cmd.exe misparses UTF-8 multibyte here
::       (a trailing byte eats the CR and a comment fragment gets executed).
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
