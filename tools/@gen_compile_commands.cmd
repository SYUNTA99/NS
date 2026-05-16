@echo off
::============================================================================
:: @gen_compile_commands.cmd
:: compile_commands.json を生成
::
:: vcvarsall.bat を実行してINCLUDE環境変数を設定した後、
:: Premakeモジュールでcompile_commands.jsonを生成する
::============================================================================

call "%~dp0_common.cmd" :init

echo compile_commands.json を生成しています...

:: MSBuild環境をセットアップ（INCLUDE環境変数が設定される）
call "%~dp0_common.cmd" :setup_msbuild
if %errorlevel% neq 0 exit /b 1

:: 日本語パス対策: TEMPにジャンクションを作成
for /f %%a in ('powershell -command "[guid]::NewGuid().ToString()"') do set "GUID=%%a"
set "JUNCTION_PATH=%TEMP%\%GUID%"
mklink /j "%JUNCTION_PATH%" "%~dp0.." >nul
pushd "%JUNCTION_PATH%"
tools\premake5.exe export-compile-commands
set "PREMAKE_RESULT=%errorlevel%"
popd

if %PREMAKE_RESULT% neq 0 (
    rmdir "%JUNCTION_PATH%"
    echo [ERROR] compile_commands.json の生成に失敗しました
    exit /b 1
)

:: Debug構成のJSONをプロジェクトルートにコピーし、パスを置換
if exist "%JUNCTION_PATH%\build\compile_commands\debug_x64.json" (
    :: パスをジャンクションから実際のパスに置換
    powershell -Command "(Get-Content '%JUNCTION_PATH%\build\compile_commands\debug_x64.json' -Raw) -replace [regex]::Escape('%JUNCTION_PATH%'.Replace('\','/')),'%CD:\=/%' | Set-Content 'compile_commands.json' -NoNewline"
    echo [OK] compile_commands.json を生成しました
) else (
    rmdir "%JUNCTION_PATH%"
    echo [ERROR] build\compile_commands\debug_x64.json が見つかりません
    exit /b 1
)

rmdir "%JUNCTION_PATH%"
