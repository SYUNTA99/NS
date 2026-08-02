@echo off
::============================================================================
:: @package_release.cmd
:: Build GameRelease and assemble a submission package under dist\NS
::
:: Usage:
::   tools\@package_release.cmd              - build then package
::   tools\@package_release.cmd --skip-build - reuse existing build
::
:: Output dist\NS\ (exe / Assets / Shaders / Source at the same level):
::   NS.exe
::   Assets\        includes Assets\Scenes\new_scene.scene (loaded on start)
::   Shaders\
::   Source\        (git-tracked files only, ThirdParty excluded)
::   premake5.lua
::
:: NOTE: ASCII-only on purpose. cmd.exe misparses UTF-8 multibyte in logic
::       lines, so this script avoids Japanese entirely.
::============================================================================
setlocal
chcp 65001 >nul

call "%~dp0_common.cmd" :init
if errorlevel 1 exit /b 1

set "CONFIG=GameRelease"
set "BIN=build\bin\%CONFIG%-windows-x86_64"
set "OUT=dist\NS"

set "SKIP_BUILD=0"
if "%~1"=="--skip-build" set "SKIP_BUILD=1"

echo ============================================
echo  NS submission package (%CONFIG%)
echo ============================================
echo.

if "%SKIP_BUILD%"=="0" (
    echo [1/5] Building %CONFIG% ...
    call "%~dp0@build.cmd" %CONFIG%
    if errorlevel 1 (
        echo [ERROR] build failed
        exit /b 1
    )
) else (
    echo [1/5] skip build
)

echo [2/5] Preparing output dir ...
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"

echo [3/5] Copying exe as NS.exe ...
if not exist "%BIN%\Game.exe" (
    echo [ERROR] %BIN%\Game.exe not found. build first
    exit /b 1
)
copy /y "%BIN%\Game.exe" "%OUT%\NS.exe" >nul

echo [4/5] Copying Assets / Shaders ...
xcopy /e /i /q /y "Assets" "%OUT%\Assets\" >nul
xcopy /e /i /q /y "Shaders" "%OUT%\Shaders\" >nul
:: Scenes now live under Assets\Scenes and are copied by the Assets xcopy above.
:: The game loads GetExeDirectory()\Assets\Scenes\new_scene.scene

echo [5/5] Exporting tracked source, ThirdParty excluded ...
git archive -o "%OUT%\_src.tar" HEAD Source/Runtime Source/Game Source/Editor Source/Tests premake5.lua
if errorlevel 1 (
    echo [ERROR] git archive failed
    exit /b 1
)
tar -xf "%OUT%\_src.tar" -C "%OUT%"
del /q "%OUT%\_src.tar"

echo.
echo ============================================
echo  Output: %OUT%\
echo    NS.exe / Assets\ / Shaders\ / Source\ / premake5.lua
echo ============================================
echo  Note: runtime needs d3dcompiler_47.dll for runtime shader compile
echo        and the VC++ redistributable, both shipped with Windows 10/11.
echo.
echo [OK] package done
exit /b 0
