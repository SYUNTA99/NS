@echo off
::============================================================================
:: @gen_compile_commands.cmd
:: Generate compile_commands.json (for the editor IntelliSense / clangd).
::
:: The real work lives in _common.cmd :gen_compile_commands. A normal build
:: (@build.cmd) calls it automatically, so run this by hand only when you want
:: to refresh the index without building.
::
:: NOTE: this header must be ASCII. It is parsed before chcp 65001 (line below)
::       takes effect, and cmd.exe misparses UTF-8 multibyte here
::       (a trailing byte eats the CR and a comment fragment gets executed).
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
