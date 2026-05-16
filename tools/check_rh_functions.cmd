@echo off
setlocal enabledelayedexpansion

:: 右手座標系関数の使用チェック
:: DirectXMathとSimpleMathの右手系関数を検出して警告を出す

set "ERROR_COUNT=0"
set "SOURCE_DIR=%~dp0..\source"

:: 一時ファイル
set "TEMP_FILE=%TEMP%\rh_check_%RANDOM%.txt"

:: source配下のファイルを検索（math_types.hは除外）
for /r "%SOURCE_DIR%" %%f in (*.cpp *.h) do (
    set "FILE=%%f"
    set "FILENAME=%%~nxf"

    :: math_types.h は除外（ラッパー定義があるため）
    if /i not "!FILENAME!"=="math_types.h" (
        :: DirectXMath 右手系関数
        findstr /i /c:"XMMatrixLookAtRH" "%%f" >nul 2>&1 && echo [WARNING] XMMatrixLookAtRH: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"XMMatrixLookToRH" "%%f" >nul 2>&1 && echo [WARNING] XMMatrixLookToRH: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"XMMatrixPerspectiveFovRH" "%%f" >nul 2>&1 && echo [WARNING] XMMatrixPerspectiveFovRH: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"XMMatrixPerspectiveRH" "%%f" >nul 2>&1 && echo [WARNING] XMMatrixPerspectiveRH: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"XMMatrixPerspectiveOffCenterRH" "%%f" >nul 2>&1 && echo [WARNING] XMMatrixPerspectiveOffCenterRH: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"XMMatrixOrthographicRH" "%%f" >nul 2>&1 && echo [WARNING] XMMatrixOrthographicRH: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"XMMatrixOrthographicOffCenterRH" "%%f" >nul 2>&1 && echo [WARNING] XMMatrixOrthographicOffCenterRH: %%f && set /a ERROR_COUNT+=1

        :: SimpleMath Matrix 右手系関数
        findstr /i /c:"Matrix::CreateLookAt" "%%f" >nul 2>&1 && echo [WARNING] Matrix::CreateLookAt: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"Matrix::CreatePerspectiveFieldOfView" "%%f" >nul 2>&1 && echo [WARNING] Matrix::CreatePerspectiveFieldOfView: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"Matrix::CreatePerspective" "%%f" >nul 2>&1 && echo [WARNING] Matrix::CreatePerspective: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"Matrix::CreateOrthographic" "%%f" >nul 2>&1 && echo [WARNING] Matrix::CreateOrthographic: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"Matrix::CreateOrthographicOffCenter" "%%f" >nul 2>&1 && echo [WARNING] Matrix::CreateOrthographicOffCenter: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"Matrix::CreateWorld" "%%f" >nul 2>&1 && echo [WARNING] Matrix::CreateWorld: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"Matrix::CreateBillboard" "%%f" >nul 2>&1 && echo [WARNING] Matrix::CreateBillboard: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"Matrix::CreateConstrainedBillboard" "%%f" >nul 2>&1 && echo [WARNING] Matrix::CreateConstrainedBillboard: %%f && set /a ERROR_COUNT+=1

        :: SimpleMath Quaternion/Vector 右手系関数
        findstr /i /c:"Quaternion::LookRotation" "%%f" >nul 2>&1 && echo [WARNING] Quaternion::LookRotation: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"Quaternion::FromToRotation" "%%f" >nul 2>&1 && echo [WARNING] Quaternion::FromToRotation: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"Vector3::Forward" "%%f" >nul 2>&1 && echo [WARNING] Vector3::Forward: %%f && set /a ERROR_COUNT+=1
        findstr /i /c:"Vector3::Backward" "%%f" >nul 2>&1 && echo [WARNING] Vector3::Backward: %%f && set /a ERROR_COUNT+=1
    )
)

if %ERROR_COUNT% gtr 0 (
    echo.
    echo ============================================================
    echo [WARNING] %ERROR_COUNT% 件の右手座標系関数が検出されました
    echo [INFO] LH:: 名前空間の左手系関数を使用してください
    echo ============================================================
    echo.
)

exit /b 0
