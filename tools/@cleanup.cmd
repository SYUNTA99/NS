@echo off
::============================================================================
:: @cleanup.cmd
:: ビルド成果物をクリーンアップするスクリプト
::
:: 削除対象:
::   - build/     (ソリューション、オブジェクトファイル、実行ファイル)
::   - .vs/       (Visual Studio設定キャッシュ)
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
