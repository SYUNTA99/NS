@echo off
::============================================================================
:: _common.cmd - Build script common library
::
:: Usage: call tools\_common.cmd :function_name
::
:: Functions:
::   :init             - Set codepage, cd to repo root
::   :check_project    - Check build/NS.sln exists
::   :setup_msbuild    - Run VsDevCmd.bat for MSBuild
::   :find_msbuild_exe - Set MSBUILD_PATH
::   :generate_project - Generate VS2022 solution via Premake5
::   :gen_compile_commands - Generate compile_commands.json (needs INCLUDE; call :setup_msbuild first)
::============================================================================

chcp 65001 >nul
goto %~1

::----------------------------------------------------------------------------
:: :init
::----------------------------------------------------------------------------
:init
    cd /d "%~dp0.."
    exit /b 0

::----------------------------------------------------------------------------
:: :check_project
::----------------------------------------------------------------------------
:check_project
    if not exist "build\NS.sln" (
        echo [ERROR] Project not found. Run tools\@make_project.cmd first.
        exit /b 1
    )
    exit /b 0

::----------------------------------------------------------------------------
:: :setup_msbuild
::----------------------------------------------------------------------------
:setup_msbuild
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "%VSWHERE%" (
        echo [ERROR] vswhere.exe not found. Install Visual Studio 2022.
        exit /b 1
    )
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find Common7\Tools\VsDevCmd.bat`) do (
        set "VSCMD_PATH=%%i"
    )
    if not defined VSCMD_PATH (
        echo [ERROR] Visual Studio not found.
        exit /b 1
    )
    call "%VSCMD_PATH%" -arch=amd64 >nul 2>&1
    exit /b 0

::----------------------------------------------------------------------------
:: :find_msbuild_exe
::----------------------------------------------------------------------------
:find_msbuild_exe
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "%VSWHERE%" (
        echo [ERROR] vswhere.exe not found. Install Visual Studio 2022.
        exit /b 1
    )
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
        set "MSBUILD_PATH=%%i"
    )
    if not defined MSBUILD_PATH (
        echo [ERROR] MSBuild not found. Install C++ workload for VS2022.
        exit /b 1
    )
    exit /b 0

::----------------------------------------------------------------------------
:: :generate_project
:: Creates junction to workaround non-ASCII path issues with Premake5
::----------------------------------------------------------------------------
:generate_project
    if not exist "%~dp0premake5.exe" (
        echo [ERROR] tools\premake5.exe が見つかりません。
        echo         初回セットアップは tools\@download_premake.cmd を実行してください。
        exit /b 1
    )
    for /f %%a in ('powershell -command "[guid]::NewGuid().ToString()"') do set "GUID=%%a"
    mklink /j "%TEMP%\%GUID%" "%~dp0.." >nul
    pushd "%TEMP%\%GUID%"
    tools\premake5.exe vs2022
    set "PREMAKE_RESULT=%errorlevel%"
    popd
    rmdir "%TEMP%\%GUID%"
    if %PREMAKE_RESULT% neq 0 (
        echo [ERROR] Project generation failed.
        exit /b 1
    )
    echo [OK] Generated build\NS.sln
    exit /b 0

::----------------------------------------------------------------------------
:: :gen_compile_commands
:: clangd (エディタの LSP) 用の compile_commands.json を生成する。
:: 事前条件: :setup_msbuild 済み (INCLUDE 環境変数が必要。 MSVC システム
::           インクルードパスをそこから取り込むため)。
:: Creates junction to workaround non-ASCII path issues with Premake5
::----------------------------------------------------------------------------
:gen_compile_commands
    if not exist "%~dp0premake5.exe" (
        echo [ERROR] tools\premake5.exe が見つかりません。
        exit /b 1
    )
    cd /d "%~dp0.."
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
    if not exist "%JUNCTION_PATH%\build\premake\compile_commands.json" (
        rmdir "%JUNCTION_PATH%"
        echo [ERROR] build\premake\compile_commands.json が見つかりません
        exit /b 1
    )
    powershell -Command "(Get-Content '%JUNCTION_PATH%\build\premake\compile_commands.json' -Raw) -replace [regex]::Escape('%JUNCTION_PATH%'.Replace('\','/')),'%CD:\=/%' | Set-Content 'compile_commands.json' -NoNewline"
    if %errorlevel% neq 0 (
        rmdir "%JUNCTION_PATH%"
        echo [ERROR] compile_commands.json の書き出しに失敗しました
        exit /b 1
    )
    rmdir "%JUNCTION_PATH%"
    echo [OK] compile_commands.json を生成しました
    exit /b 0
