@echo off
::============================================================================
:: @build_tests.cmd
:: Generate the VS solution via Premake5, then build only the test executable
:: (Tests, googletest and the Runtime libraries Tests links) with MSBuild.
:: Game / Editor / GameApp are not built.
::
:: Usage: Tools\@build_tests.cmd [Debug|Development|GameDebug]
::   Defaults to Debug when omitted.
::   GameRelease is rejected: Tests is kind "None" there (premake5.lua), so
::   MSBuild would report success without building anything.
::
:: Output:
::   build\bin\<Config>-windows-x86_64\Tests\Tests.exe
::   Run it with Tools\@run_tests.cmd nobuild <Config>.
::
:: NOTE: this whole file must be ASCII. cmd.exe misparses UTF-8 multibyte even
::       after chcp 65001 (a trailing byte eats the CR and a fragment of the
::       next line gets executed).
::============================================================================
setlocal
chcp 65001 >nul

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"

if /i "%CONFIG%"=="GameRelease" (
    echo [ERROR] GameRelease has no tests. Use Debug, Development or GameDebug.
    exit /b 1
)

:: Same limit as @build.cmd: 10 of the 20 physical cores, no worker nodes left behind
set "MSBUILD_CPUS=10"

call "%~dp0_common.cmd" :init
if errorlevel 1 exit /b 1

call "%~dp0_common.cmd" :generate_project
if errorlevel 1 exit /b 1

echo.
echo Building tests (%CONFIG%)...
call "%~dp0_common.cmd" :setup_msbuild
if errorlevel 1 exit /b 1

:: Tests sits in the "_Tests" solution folder (group "_Tests" in premake5.lua),
:: so its target name carries the folder. Only its project references are built
msbuild build\NS.sln -t:_Tests\Tests /p:Configuration=%CONFIG% /p:Platform=x64 /m:%MSBUILD_CPUS% -nodeReuse:false /v:minimal
if errorlevel 1 (
    echo [WARN] Parallel build failed. Retrying with /m:1...
    msbuild build\NS.sln -t:_Tests\Tests /p:Configuration=%CONFIG% /p:Platform=x64 /m:1 -nodeReuse:false /v:minimal
    if errorlevel 1 (
        echo [ERROR] Test build failed
        exit /b 1
    )
)

echo [OK] Test build succeeded
exit /b 0
