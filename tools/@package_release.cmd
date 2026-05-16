@echo off
::============================================================================
:: @package_release.cmd
:: リリースパッケージ作成スクリプト
::
:: 使用方法:
::   tools\@package_release.cmd              - ビルド後にパッケージ作成
::   tools\@package_release.cmd --skip-build - ビルドをスキップ
::
:: 出力: release/
::   ├── 実行環境/
::   │   ├── tests.exe
::   │   ├── assimp-vc143-mt.dll
::   │   └── assets/
::   └── source/
::============================================================================
call "%~dp0_common.cmd" :init

set OUTPUT_DIR=release
set RUNTIME_DIR=%OUTPUT_DIR%\実行環境
set SOURCE_DIR=%OUTPUT_DIR%\source

:: パラメータ確認
set SKIP_BUILD=0
if "%1"=="--skip-build" set SKIP_BUILD=1

echo ============================================
echo  NS-ENGINE リリースパッケージ作成
echo ============================================
echo.

:: 1. Releaseビルド
if %SKIP_BUILD%==0 (
    echo [1/5] Release ビルド中...
    call "%~dp0@build.cmd" Release
    if errorlevel 1 (
        echo [ERROR] ビルドに失敗しました
        exit /b 1
    )
    echo.
) else (
    echo [1/5] ビルドをスキップ
)

:: 2. 既存の出力ディレクトリをクリーンアップ
echo [2/5] 出力ディレクトリ準備中...
if exist "%OUTPUT_DIR%" (
    echo   既存の %OUTPUT_DIR% を削除中...
    rmdir /s /q "%OUTPUT_DIR%"
)
mkdir "%RUNTIME_DIR%"
mkdir "%SOURCE_DIR%"

:: 3. 実行環境をコピー
echo [3/5] 実行環境をコピー中...

:: tests.exe
set EXE_PATH=build\bin\Release-windows-x86_64\tests\tests.exe
if not exist "%EXE_PATH%" (
    echo [ERROR] %EXE_PATH% が見つかりません
    echo         先にビルドを実行してください: tools\@build.cmd Release
    exit /b 1
)
echo   tests.exe をコピー中...
copy /y "%EXE_PATH%" "%RUNTIME_DIR%\" >nul

:: assimp DLL
set DLL_PATH=build\bin\Release-windows-x86_64\tests\assimp-vc143-mt.dll
if not exist "%DLL_PATH%" (
    :: フォールバック: 元の場所からコピー
    set DLL_PATH=external\assimp\bin\Release\assimp-vc143-mt.dll
)
if not exist "%DLL_PATH%" (
    echo [ERROR] assimp-vc143-mt.dll が見つかりません
    exit /b 1
)
echo   assimp-vc143-mt.dll をコピー中...
copy /y "%DLL_PATH%" "%RUNTIME_DIR%\" >nul

:: assets フォルダ（テストフォルダを除外）
echo   assets フォルダをコピー中...
if exist "assets\shader" xcopy /e /i /q /y "assets\shader" "%RUNTIME_DIR%\assets\shader\" >nul
if exist "assets\texture" xcopy /e /i /q /y "assets\texture" "%RUNTIME_DIR%\assets\texture\" >nul
if exist "assets\model" xcopy /e /i /q /y "assets\model" "%RUNTIME_DIR%\assets\model\" >nul
if exist "assets\material" xcopy /e /i /q /y "assets\material" "%RUNTIME_DIR%\assets\material\" >nul

:: README をコピー（存在する場合）
if exist "tools\templates\README_実行環境.txt" (
    copy /y "tools\templates\README_実行環境.txt" "%RUNTIME_DIR%\README.txt" >nul
)

:: 4. ソースコードをコピー
echo [4/5] ソースコードをコピー中...
if exist "source\common" xcopy /e /i /q /y "source\common" "%SOURCE_DIR%\common\" >nul
if exist "source\dx11" xcopy /e /i /q /y "source\dx11" "%SOURCE_DIR%\dx11\" >nul
if exist "source\engine" xcopy /e /i /q /y "source\engine" "%SOURCE_DIR%\engine\" >nul

:: premake5.lua と主要な設定ファイルもコピー
if exist "premake5.lua" copy /y "premake5.lua" "%SOURCE_DIR%\" >nul

:: 5. 完了メッセージ
echo [5/5] パッケージ作成完了
echo.
echo ============================================
echo  出力先: %OUTPUT_DIR%\
echo ============================================
echo.
echo  実行環境\
echo    - tests.exe
echo    - assimp-vc143-mt.dll
echo    - assets\ (shader, texture, model, material)
echo.
echo  source\
echo    - common, dx11, engine
echo    - premake5.lua
echo.

:: サイズ表示
echo  フォルダサイズ:
for /f "tokens=3" %%a in ('dir /s /-c "%RUNTIME_DIR%" 2^>nul ^| findstr /c:"個のファイル"') do (
    set /a SIZE_MB=%%a / 1048576
    echo    実行環境: 約 !SIZE_MB! MB
)
for /f "tokens=3" %%a in ('dir /s /-c "%SOURCE_DIR%" 2^>nul ^| findstr /c:"個のファイル"') do (
    set /a SIZE_MB=%%a / 1048576
    echo    source: 約 !SIZE_MB! MB
)

echo.
echo [OK] パッケージ作成成功
exit /b 0
