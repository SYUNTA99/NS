@echo off
::============================================================================
:: @run_tests.cmd
:: Build the tests, then run them.
::
:: Usage: tools\@run_tests.cmd [nobuild] [Debug|Development|GameDebug|GameRelease] [gtest_filter]
::   nobuild  skip generation/build, run the existing tests.exe as-is
::            (keeps the PC load low; build first with tools\@build.cmd)
::   Config   defaults to Debug when omitted
::   filter   defaults to all tests. e.g. CameraBrainTest.*:ChunkIOTest.*
::
:: NOTE: this header must be ASCII. It is parsed before chcp 65001 (line below)
::       takes effect, and cmd.exe misparses UTF-8 multibyte here
::       (a trailing byte eats the CR and a comment fragment gets executed).
::============================================================================
setlocal
chcp 65001 >nul

set "NOBUILD="
if /i "%~1"=="nobuild" (
    set "NOBUILD=1"
    shift /1
)

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
set "FILTER=%~2"

call "%~dp0_common.cmd" :init
if errorlevel 1 exit /b 1

if defined NOBUILD goto run_tests

:: PC を張り付かせないよう msbuild の並列数を 10 に制限する (物理 20 コア中 10、 残りは OS / 編集用)
set "MSBUILD_CPUS=10"

echo ===================================
echo テストビルド・実行 (%CONFIG%, msbuild -m:%MSBUILD_CPUS%)
echo ===================================
echo.

echo [1/3] プロジェクト生成中...
call "%~dp0_common.cmd" :generate_project
if errorlevel 1 exit /b 1

echo.
echo [2/3] テストビルド中...
call "%~dp0_common.cmd" :setup_msbuild
if errorlevel 1 exit /b 1

msbuild build\NS.sln /p:Configuration=%CONFIG% /p:Platform=x64 /m:%MSBUILD_CPUS% -nodeReuse:false /v:minimal
if errorlevel 1 (
    echo [WARN] 並列ビルド失敗。 /m:1 で再試行します...
    msbuild build\NS.sln /p:Configuration=%CONFIG% /p:Platform=x64 /m:1 -nodeReuse:false /v:minimal
    if errorlevel 1 (
        echo [ERROR] ビルド失敗
        exit /b 1
    )
)

echo.
echo [3/3] テスト実行中...

:run_tests
set "TEST_EXE=build\bin\%CONFIG%-windows-x86_64\tests\tests.exe"
if not exist "%TEST_EXE%" (
    echo [ERROR] テスト実行ファイルが見つかりません: %TEST_EXE%
    echo         先に tools\@build.cmd %CONFIG% でビルドしてください。
    exit /b 1
)

echo ===================================
if defined NOBUILD (
    if "%FILTER%"=="" (
        echo テスト実行 (%CONFIG%, build なし, 全件^)
    ) else (
        echo テスト実行 (%CONFIG%, build なし, filter=%FILTER%^)
    )
    echo ===================================
)

if "%FILTER%"=="" (
    "%TEST_EXE%" --gtest_color=yes
) else (
    "%TEST_EXE%" --gtest_color=yes --gtest_filter=%FILTER%
)
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
