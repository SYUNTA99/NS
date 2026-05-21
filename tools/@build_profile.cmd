@echo off
setlocal

REM ----------------------------------------------------------------------------
REM NS_ENABLE_PROFILING を有効化した Debug build を 1 cmd で作成する。
REM clock.h の NS_SCOPED_TIMER が展開され、 CharacterController::Update /
REM CharacterMovement::OnUpdate / Application::FixedStepLoop の measurement が
REM Debug log に出力される。
REM
REM setlocal 範囲内で env var を設定するため、 shell 汚染なし。
REM 終了後は通常 `tools\@build.cmd Debug` で profiling 無し build に戻せる。
REM ----------------------------------------------------------------------------

set NS_ENABLE_PROFILING=1
echo [build_profile] NS_ENABLE_PROFILING=1 で profile build を作成します

call "%~dp0@regen_project.cmd"
if errorlevel 1 (
    echo [build_profile] regen 失敗
    exit /b %errorlevel%
)

call "%~dp0@build.cmd" Debug
exit /b %errorlevel%
