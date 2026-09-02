@echo off
::============================================================================
:: @open_project.cmd
:: Build if needed, then launch Visual Studio.
::
:: Steps:
::   1. Set codepage, cd to repo root (:init)
::   2. Generate the solution if missing (:generate_project)
::   3. Build Debug if tests.exe is missing
::   4. Open the solution in Visual Studio
::
:: Use: double-click at the start of a dev session to prepare the environment.
::
:: NOTE: this header must be ASCII. It is parsed before chcp 65001 takes effect
::       (via _common.cmd :init), and cmd.exe misparses UTF-8 multibyte here
::       (a trailing byte eats the CR and a comment fragment gets executed).
::============================================================================
call "%~dp0_common.cmd" :init

echo 現在のディレクトリ: %CD%
echo.

:: ソリューションが無ければ生成
if not exist "build\NS.sln" (
    echo プロジェクト生成中...
    call "%~dp0_common.cmd" :generate_project
    if errorlevel 1 (
        pause
        exit /b 1
    )
) else (
    echo [OK] build\NS.sln 確認済み
)

:: tests.exeが存在すればビルドをスキップ
if exist "build\bin\Debug-windows-x86_64\tests\tests.exe" goto :skip_build

echo.
echo Debug ビルド中...
call "%~dp0_common.cmd" :find_msbuild_exe
if errorlevel 1 (
    pause
    exit /b 1
)
echo 使用: %MSBUILD_PATH%
"%MSBUILD_PATH%" build\NS.sln -p:Configuration=Debug -p:Platform=x64 -m -v:minimal
if errorlevel 1 (
    echo [ERROR] ビルド失敗
    pause
    exit /b 1
)
echo [OK] ビルド成功
goto :open_vs

:skip_build
echo [OK] tests.exe 確認済み

:open_vs

echo.
echo Visual Studio を起動中...
start "" "build\NS.sln"
