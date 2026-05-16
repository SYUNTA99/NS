@echo off
::============================================================================
:: @open_project.cmd
:: 必要に応じてビルドし、Visual Studioを起動するスクリプト
::
:: 処理内容:
::   1. UTF-8モード設定 & 作業ディレクトリ移動 (:init)
::   2. ソリューションが無ければ生成 (:generate_project)
::   3. tests.exeが無ければDebugビルド実行
::   4. Visual Studioでソリューションを開く
::
:: 用途:
::   開発開始時にダブルクリックで環境を準備
::============================================================================
call "%~dp0_common.cmd" :init

echo 現在のディレクトリ: %CD%
echo.

:: ソリューションが無ければ生成
if not exist "build\NS-ENGINE.sln" (
    echo プロジェクト生成中...
    call "%~dp0_common.cmd" :generate_project
    if errorlevel 1 (
        pause
        exit /b 1
    )
) else (
    echo [OK] build\NS-ENGINE.sln 確認済み
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
"%MSBUILD_PATH%" build\NS-ENGINE.sln -p:Configuration=Debug -p:Platform=x64 -m -v:minimal
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
start "" "build\NS-ENGINE.sln"
