@echo off
::============================================================================
:: @gen_compile_commands.cmd
:: compile_commands.json を生成 (clangd / エディタの IntelliSense 用)
::
:: 実体は _common.cmd の :gen_compile_commands に集約。
:: 通常ビルド (@build.cmd) では自動で呼ばれるため、 手動実行は
:: ビルドせずにインデックスだけ更新したい場合に使う。
::============================================================================
setlocal
chcp 65001 >nul

call "%~dp0_common.cmd" :init
if errorlevel 1 exit /b 1

echo compile_commands.json を生成しています...

:: MSBuild環境をセットアップ（INCLUDE環境変数が設定される）
call "%~dp0_common.cmd" :setup_msbuild
if errorlevel 1 exit /b 1

call "%~dp0_common.cmd" :gen_compile_commands
if errorlevel 1 exit /b 1

exit /b 0
