@echo off
::============================================================================
:: @build_effects.cmd
:: Build efkprobe, then turn the effect definitions into .efkefc and check them.
::
:: Usage: Tools\@build_effects.cmd [nobuild] [names ...] [--frames N] [--no-probe] [--install]
::   nobuild    skip generation/build, use the existing efkprobe.exe as-is
::   names      definitions in Tools\effects\defs (without .efkproj). All when omitted
::   --install  after every effect passes, copy the .efkefc files and Texture\ to Assets\Effects
::
:: Needs the environment variable NS_EFFEKSEER_TOOL (the Effekseer 1.80.7 Tool folder),
:: Python with Pillow, and the .NET 9 SDK. See Tools\effects\README.md.
:: Work files go to build\effects. efkprobe is built in Debug (project EffectProbe).
::
:: NOTE: this header must be ASCII. It is parsed before chcp 65001 (line below)
::       takes effect, and cmd.exe misparses UTF-8 multibyte here
::       (a trailing byte eats the CR and a comment fragment gets executed).
::============================================================================
setlocal
chcp 65001 >nul

set "NOBUILD="
if /i "%~1"=="nobuild" (
    set "NOBUILD=1"
    shift /1
)

set "EFFECT_ARGS="
:collect_args
if "%~1"=="" goto args_collected
set EFFECT_ARGS=%EFFECT_ARGS% %1
shift /1
goto collect_args
:args_collected

call "%~dp0_common.cmd" :init
if errorlevel 1 exit /b 1

if defined NOBUILD goto run_effects

call "%~dp0_common.cmd" :generate_project
if errorlevel 1 exit /b 1

call "%~dp0_common.cmd" :setup_msbuild
if errorlevel 1 exit /b 1

:: 絵の道に要るのは efkprobe と、それが繋ぐ effekseer の lib だけなので、その 2 つだけを組む
msbuild build\NS.sln -t:_Tools\EffectProbe -p:Configuration=Debug -p:Platform=x64 -m:10 -nodeReuse:false -v:minimal
if errorlevel 1 (
    echo [ERROR] efkprobe を組めなかった
    exit /b 1
)

:run_effects
set "PYTHONIOENCODING=utf-8"
python Tools\effects\efkbuild.py %EFFECT_ARGS%
set "EFFECT_RESULT=%errorlevel%"

echo.
if "%EFFECT_RESULT%"=="0" (
    echo [OK] 全部の絵が書き出し・照合・efkprobe を通った
) else (
    echo [FAILED] 通らなかった絵がある
)
exit /b %EFFECT_RESULT%
