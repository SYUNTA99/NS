@echo off
::============================================================================
:: @build.cmd
:: Generate the VS solution via Premake5, then build the given config w/ MSBuild.
::
:: Usage: Tools\@build.cmd [Debug|Development|GameDebug|GameRelease] [profile]
::   Defaults to Debug when omitted.
::   profile: define NS_ENABLE_PROFILING so NS_SCOPED_TIMER expands.
::            (CharacterMovement::OnUpdate / CapsuleMover::Update /
::             Application::FixedStepLoop timings go to the Debug log.
::             Rerun without "profile" to get a normal build back.)
::
:: NOTE: this header must be ASCII. It is parsed before chcp 65001 (line below)
::       takes effect, and cmd.exe misparses UTF-8 multibyte here
::       (a trailing byte eats the CR and a comment fragment gets executed).
::============================================================================
setlocal
chcp 65001 >nul

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"

:: profile 指定で計測ビルド。premake が環境変数を読んで define を注入する (premake5.lua 参照)
if /i "%~2"=="profile" (
    set "NS_ENABLE_PROFILING=1"
    echo [build] NS_ENABLE_PROFILING=1 で計測ビルドを作成します
)

:: PC を張り付かせないよう msbuild の並列数を 10 に制限する (物理 20 コア中 10、 残りは OS / 編集用)
:: -nodeReuse:false はビルド後にワーカーを残さない。 既定だと 10 個が居座って 500MB 超を占め続ける
set "MSBUILD_CPUS=10"

call "%~dp0_common.cmd" :init
if errorlevel 1 exit /b 1

call "%~dp0_common.cmd" :generate_project
if errorlevel 1 exit /b 1

echo.
echo %CONFIG% ビルド...
call "%~dp0_common.cmd" :setup_msbuild
if errorlevel 1 exit /b 1

:: IntelliSense 用インデックス (compile_commands.json) を最新化 (失敗してもビルドは続行)
call "%~dp0_common.cmd" :gen_compile_commands
if errorlevel 1 echo [WARN] compile_commands.json の生成に失敗しましたが、ビルドを続行します

msbuild build\NS.sln /p:Configuration=%CONFIG% /p:Platform=x64 /m:%MSBUILD_CPUS% -nodeReuse:false /v:minimal
if errorlevel 1 (
    echo [WARN] 並列ビルド失敗。 /m:1 で再試行します...
    msbuild build\NS.sln /p:Configuration=%CONFIG% /p:Platform=x64 /m:1 -nodeReuse:false /v:minimal
    if errorlevel 1 (
        echo [ERROR] ビルド失敗
        exit /b 1
    )
)

echo [OK] ビルド成功
exit /b 0
