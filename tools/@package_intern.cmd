@echo off
::============================================================================
:: @package_intern.cmd
:: Build GameRelease and assemble an uncompressed internship submission folder.
::
:: Output layout (the runnable set lives in Game\ so the exe sits next to its
:: DLLs and the engine's exe-adjacent ContentRoot resolves Shaders\ Assets\):
::   <OUT>\
::     Game\                     runnable set (run Game.exe from here)
::       Game.exe                  executable (GameRelease, editor excluded)
::       Assets\                 asset folder
::       Shaders\                runtime-compiled HLSL (required to run)
::       Scenes\                 new_scene.scene (the scene the game loads on start)
::       *.dll                   d3dcompiler_47 + VC++ runtime (exe-adjacent)
::     Source\                   own source only (ThirdParty excluded)
::       THIRD_PARTY_NOTICES.txt notices for third-party libs embedded in Game.exe
::       Licenses\               full third-party license texts
::     README.txt
::
:: Usage:
::   tools\@package_intern.cmd                 build GameRelease then package
::   tools\@package_intern.cmd --skip-build    reuse existing GameRelease build
::   tools\@package_intern.cmd <outdir>        custom output dir (default dist\NS_intern)
::   (flags and outdir can be combined in any order)
::
:: NOTE: ASCII-only logic on purpose. cmd.exe misparses UTF-8 multibyte in
::       logic lines. README / NOTICES are separate UTF-8 files, copied as-is.
::============================================================================
setlocal enabledelayedexpansion
chcp 65001 >nul

call "%~dp0_common.cmd" :init
if errorlevel 1 exit /b 1

set "CONFIG=GameRelease"
set "BIN=build\bin\%CONFIG%-windows-x86_64"
set "OUT=dist\NS_intern"
set "ASSETS=%~dp0package_assets"
set "SKIP_BUILD=0"

:: --- arg parse: --skip-build flag, anything else = output dir ---
for %%A in (%*) do (
    if /i "%%~A"=="--skip-build" (
        set "SKIP_BUILD=1"
    ) else (
        set "OUT=%%~A"
    )
)

set "GAMEDIR=%OUT%\Game"

echo ============================================
echo  NS internship package  (%CONFIG%)
echo  output: %OUT%
echo ============================================
echo.

:: --- 1. build GameRelease -------------------------------------------------
if "%SKIP_BUILD%"=="0" (
    echo [1/8] Building %CONFIG% ...
    call "%~dp0@build.cmd" %CONFIG%
    if errorlevel 1 (
        echo [ERROR] build failed
        exit /b 1
    )
) else (
    echo [1/8] skip build
)

:: --- 2. set up VS env so VCToolsRedistDir is available for CRT DLLs --------
echo [2/8] Locating Visual C++ runtime ...
call "%~dp0_common.cmd" :setup_msbuild
if errorlevel 1 (
    echo [WARN] VS env not set; will fall back to System32 for runtime DLLs
)

:: --- 3. verify exe --------------------------------------------------------
if not exist "%BIN%\Game.exe" (
    echo [ERROR] %BIN%\Game.exe not found. Build %CONFIG% first.
    exit /b 1
)

:: --- 4. fresh output dir --------------------------------------------------
echo [3/8] Preparing output dir ...
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"
mkdir "%GAMEDIR%"

:: --- 5. exe + assets (into Game\ so exe sits next to its DLLs) -------------
echo [4/8] Copying exe + Assets + Shaders + Scenes into Game\ ...
copy /y "%BIN%\Game.exe" "%GAMEDIR%\Game.exe" >nul
xcopy /e /i /q /y "Assets"  "%GAMEDIR%\Assets\"  >nul
xcopy /e /i /q /y "Shaders" "%GAMEDIR%\Shaders\" >nul
:: Scenes: repo is the source of truth. The game loads GetExeDirectory()\Scenes\new_scene.scene
if exist "Scenes" (
    xcopy /e /i /q /y "Scenes" "%GAMEDIR%\Scenes\" >nul
) else (
    echo [WARN] Scenes\ not found at repo root -- game will fall back to the seed floor
)

:: --- 6. runtime dependencies (DLLs) --------------------------------------
echo [5/8] Bundling runtime dependencies ...

:: 6a. d3dcompiler_47.dll (runtime HLSL compile) -- always present on Win10/11
if exist "%SystemRoot%\System32\d3dcompiler_47.dll" (
    copy /y "%SystemRoot%\System32\d3dcompiler_47.dll" "%GAMEDIR%\" >nul
    echo       + d3dcompiler_47.dll
) else (
    echo [WARN] d3dcompiler_47.dll not found in System32 -- add it manually
)

:: 6b. VC++ runtime -- only the DLLs Game.exe actually imports (dumpbin verified):
::     vcruntime140.dll / vcruntime140_1.dll / msvcp140.dll
::     prefer the VS redist (app-local deploy), fall back to System32 per file
set "CRT_SRC="
if defined VCToolsRedistDir (
    for /d %%D in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do set "CRT_SRC=%%~fD"
)
for %%F in (vcruntime140.dll vcruntime140_1.dll msvcp140.dll) do (
    if defined CRT_SRC if exist "!CRT_SRC!\%%F" copy /y "!CRT_SRC!\%%F" "%GAMEDIR%\" >nul
    if not exist "%GAMEDIR%\%%F" if exist "%SystemRoot%\System32\%%F" copy /y "%SystemRoot%\System32\%%F" "%GAMEDIR%\" >nul
)
echo       + VC++ runtime (vcruntime140, vcruntime140_1, msvcp140)

:: --- 7. own source (ThirdParty excluded) + docs + license texts -----------
echo [6/8] Exporting own source (ThirdParty excluded) ...
git archive -o "%OUT%\_src.tar" HEAD Source/Runtime Source/Game Source/Editor
if errorlevel 1 (
    echo [ERROR] git archive failed
    exit /b 1
)
tar -xf "%OUT%\_src.tar" -C "%OUT%"
del /q "%OUT%\_src.tar"

echo [7/8] Adding README (top) + notices/licenses inside Source\ ...
copy /y "%ASSETS%\README.txt"                "%OUT%\README.txt"                        >nul
copy /y "%ASSETS%\THIRD_PARTY_NOTICES.txt"   "%OUT%\Source\THIRD_PARTY_NOTICES.txt"    >nul
mkdir "%OUT%\Source\Licenses"
copy /y "Source\ThirdParty\DirectXTK\LICENSE"   "%OUT%\Source\Licenses\DirectXTK-LICENSE.txt"   >nul
copy /y "Source\ThirdParty\DirectXTex\LICENSE"  "%OUT%\Source\Licenses\DirectXTex-LICENSE.txt"  >nul
copy /y "Source\ThirdParty\spdlog\LICENSE"      "%OUT%\Source\Licenses\spdlog-LICENSE.txt"      >nul
copy /y "Source\ThirdParty\magic_enum\LICENSE"  "%OUT%\Source\Licenses\magic_enum-LICENSE.txt"  >nul

:: --- 8. rename ASCII build folders to JP submission names ------------------
:: cmd misparses UTF-8 in logic lines, so the JP rename is done in a pure-ASCII
:: PowerShell script (Game -> exec-env folder, Source -> source-code folder).
echo [8/8] Renaming folders to Japanese names ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_assets\rename_folders.ps1" "%OUT%"
if errorlevel 1 (
    echo [ERROR] folder rename failed
    exit /b 1
)

echo.
echo ============================================
echo  Done (uncompressed):  %OUT%\
echo    [exec-env folder]  Game.exe + Assets + Shaders + Scenes + DLLs
echo    [source folder]    own code + THIRD_PARTY_NOTICES.txt + Licenses\
echo    README.txt
echo ============================================
echo  Run Game.exe inside the execution-environment folder. Submit as-is (no zip).
echo.
echo [OK] package done
exit /b 0
