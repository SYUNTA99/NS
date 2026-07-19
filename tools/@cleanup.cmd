@echo off
::============================================================================
:: @cleanup.cmd
:: Remove build artifacts.
::
:: Deletes:
::   - build/     (solution, object files, executables)
::   - .vs/       (Visual Studio settings cache)
::
:: NOTE: this header must be ASCII. It is parsed before chcp 65001 takes effect
::       (via _common.cmd :init), and cmd.exe misparses UTF-8 multibyte here
::       (a trailing byte eats the CR and a comment fragment gets executed).
::============================================================================
call "%~dp0_common.cmd" :init

echo ===================================
echo クリーンアップ
echo ===================================

:: build/ フォルダを削除
if exist "build" (
    echo build/ を削除中...
    rmdir /s /q build
    echo   [OK] build/ を削除しました
) else (
    echo   build/ は存在しません
)

:: .vs/ フォルダを削除（Visual Studio設定キャッシュ）
if exist ".vs" (
    echo .vs/ を削除中...
    rmdir /s /q .vs
    echo   [OK] .vs/ を削除しました
) else (
    echo   .vs/ は存在しません
)

echo.
echo [OK] クリーンアップ完了
