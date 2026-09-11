@echo off
::============================================================================
:: @package_release.cmd
:: Build GameRelease and assemble an uncompressed release submission folder.
::
:: Output layout (the runnable set lives in Game\ so the exe sits next to its
:: DLLs and the engine's exe-adjacent ContentRoot resolves Shaders\ Assets\):
::   <OUT>\
::     Game\                     runnable set (run Game.exe from here)
::       Game.exe                  executable (GameRelease, editor excluded)
::       Assets\                 asset folder (includes Assets\Scenes\new_scene.scene)
::       Shaders\                runtime-compiled HLSL (required to run)
::       *.dll                   d3dcompiler_47 + VC++ runtime (exe-adjacent)
::     Source\                   own source only (ThirdParty excluded)
::       THIRD_PARTY_NOTICES.txt notices for third-party libs embedded in Game.exe
::       Licenses\               full third-party license texts
::     README.txt
::
:: Usage:
::   Tools\@package_release.cmd                 build GameRelease then package
::   Tools\@package_release.cmd --skip-build    reuse existing GameRelease build
::   Tools\@package_release.cmd <outdir>        custom output dir (default NS_release under the repo root)
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
set "OUT=NS_release"
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

set "ZIP=%OUT%.zip"
for %%I in ("%OUT%") do set "OUTPARENT=%%~dpI"
if not exist "%OUTPARENT%" (
    echo [ERROR] output drive/folder not found: %OUTPARENT%
    exit /b 1
)

set "GAMEDIR=%OUT%\Game"

echo ============================================
echo  NS release package  (%CONFIG%)
echo  output: %OUT%
echo ============================================
echo.

:: --- 1. build GameRelease -------------------------------------------------
if "%SKIP_BUILD%"=="0" (
    echo [1/9] Building %CONFIG% ...
    call "%~dp0@build.cmd" %CONFIG%
    if errorlevel 1 (
        echo [ERROR] build failed
        exit /b 1
    )
) else (
    echo [1/9] skip build
)

:: --- 2. set up VS env so VCToolsRedistDir is available for CRT DLLs --------
echo [2/9] Locating Visual C++ runtime ...
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
:: The delete is delegated to PowerShell: rmdir /s /q is denied on the
:: previous run's .txt / .png files here. clean_dir.ps1 has the measurement.
echo [3/9] Preparing output dir ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_assets\clean_dir.ps1" "%OUT%"
if errorlevel 1 (
    echo [ERROR] could not clear %OUT%
    exit /b 1
)
mkdir "%OUT%"
if errorlevel 1 (
    echo [ERROR] mkdir %OUT% failed
    exit /b 1
)
mkdir "%GAMEDIR%"
if errorlevel 1 (
    echo [ERROR] mkdir %GAMEDIR% failed
    exit /b 1
)

:: --- 5. exe + assets (into Game\ so exe sits next to its DLLs) -------------
echo [4/9] Copying exe + Assets + Shaders into Game\ ...
copy /y "%BIN%\Game.exe" "%GAMEDIR%\Game.exe" >nul
if errorlevel 1 (
    echo [ERROR] copy Game.exe failed
    exit /b 1
)
xcopy /e /i /q /y "Assets"  "%GAMEDIR%\Assets\"  >nul
if errorlevel 1 (
    echo [ERROR] copy Assets failed
    exit /b 1
)
xcopy /e /i /q /y "Shaders" "%GAMEDIR%\Shaders\" >nul
if errorlevel 1 (
    echo [ERROR] copy Shaders failed
    exit /b 1
)
:: Scenes now live under Assets\Scenes and are copied by the Assets xcopy above.
:: The game loads GetExeDirectory()\Assets\Scenes\new_scene.scene
if not exist "%GAMEDIR%\Assets\Scenes\new_scene.scene" (
    echo [WARN] Assets\Scenes\new_scene.scene missing -- game will boot into an empty scene
)

:: --- 6. runtime dependencies (DLLs) --------------------------------------
echo [5/9] Bundling runtime dependencies ...

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
echo [6/9] Exporting own source (ThirdParty excluded) ...
git archive -o "%OUT%\_src.tar" HEAD Source/Runtime Source/Game Source/Editor
if errorlevel 1 (
    echo [ERROR] git archive failed
    exit /b 1
)
:: tar reads a leading "X:" as a remote host, so the archive is extracted from
:: inside the output dir with a relative name.
pushd "%OUT%"
tar -xf "_src.tar"
set "TAR_RESULT=%errorlevel%"
popd
if not "%TAR_RESULT%"=="0" (
    echo [ERROR] extracting the source archive failed
    exit /b 1
)
del /q "%OUT%\_src.tar"

echo [7/9] Adding README (top) + notices/licenses inside Source\ ...
copy /y "%ASSETS%\README.txt"                "%OUT%\README.txt"                        >nul
if errorlevel 1 (
    echo [ERROR] copy README.txt failed
    exit /b 1
)
copy /y "%ASSETS%\THIRD_PARTY_NOTICES.txt"   "%OUT%\Source\THIRD_PARTY_NOTICES.txt"    >nul
if errorlevel 1 (
    echo [ERROR] copy THIRD_PARTY_NOTICES.txt failed
    exit /b 1
)
mkdir "%OUT%\Source\Licenses"
if errorlevel 1 (
    echo [ERROR] mkdir %OUT%\Source\Licenses failed
    exit /b 1
)
:: one license text per bundled library: ThirdParty\<lib>\LICENSE -> <lib>-LICENSE.txt
for %%L in (DirectXTK DirectXTex spdlog magic_enum) do (
    copy /y "Source\ThirdParty\%%L\LICENSE" "%OUT%\Source\Licenses\%%L-LICENSE.txt" >nul
    if errorlevel 1 (
        echo [ERROR] copy %%L LICENSE failed
        exit /b 1
    )
)

:: --- 8. rename ASCII build folders to JP submission names ------------------
:: cmd misparses UTF-8 in logic lines, so the JP rename is done in a pure-ASCII
:: PowerShell script (Game -> exec-env folder, Source -> source-code folder).
echo [8/9] Renaming folders to Japanese names ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_assets\rename_folders.ps1" "%OUT%"
if errorlevel 1 (
    echo [ERROR] folder rename failed
    exit /b 1
)

:: --- 9. zip for handover ---------------------------------------------------
:: Compress-Archive leaves the UTF-8 flag unset and the JP folder names come out
:: mojibake, so the archive is built in a pure-ASCII PowerShell script.
echo [9/9] Creating zip ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package_assets\make_zip.ps1" "%OUT%" "%ZIP%"
if errorlevel 1 (
    echo [ERROR] zip failed
    exit /b 1
)

echo.
echo ============================================
echo  Done:  %ZIP%
echo  Folder (uncompressed):  %OUT%\
echo    [exec-env folder]  Game.exe + Assets + Shaders + DLLs
echo    [source folder]    own code + THIRD_PARTY_NOTICES.txt + Licenses\
echo    README.txt
echo ============================================
echo  Run Game.exe in the folder to check, hand over the zip.
echo.
echo [OK] package done
exit /b 0
