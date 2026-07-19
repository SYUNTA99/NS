@echo off
::============================================================================
:: @make_project.cmd
:: Generate the VS2022 solution then run a full Debug build in one shot.
::
:: Usage:
::   tools\@make_project.cmd
::
:: Steps:
::   1. Set codepage, cd to repo root (:init)
::   2. Remove build/ (clean)
::   3. Generate the VS2022 solution via Premake5 (:generate_project)
::   4. Set up the MSBuild environment via VsDevCmd.bat (:setup_msbuild)
::   5. Build Debug with MSBuild
::
:: Output:
::   build\NS.sln
::   build\bin\Debug-windows-x86_64\tests\tests.exe
::
:: NOTE: this header must be ASCII. It is parsed before chcp 65001 takes effect
::       (via _common.cmd :init), and cmd.exe misparses UTF-8 multibyte here
::       (a trailing byte eats the CR and a comment fragment gets executed).
::============================================================================
call "%~dp0_common.cmd" :init

:: クリーンアップ
if exist "build" (
    echo build/ フォルダを削除中...
    rmdir /s /q build
)

echo Visual Studio 2022 プロジェクトを生成中...
call "%~dp0_common.cmd" :generate_project
if errorlevel 1 exit /b 1

echo.
echo Debug ビルド中...
call "%~dp0_common.cmd" :setup_msbuild
if errorlevel 1 exit /b 1

msbuild build\NS.sln -p:Configuration=Debug -p:Platform=x64 -m -v:minimal
if errorlevel 1 (
    echo [ERROR] ビルド失敗
    exit /b 1
)
echo [OK] ビルド成功
