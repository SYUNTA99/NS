@echo off
::============================================================================
:: @regen_project.cmd
:: Visual Studio ソリューションを再生成するスクリプト
::
:: 使用方法: tools\@regen_project.cmd
::============================================================================
chcp 65001 >nul
call "%~dp0_common.cmd" :generate_project
pause
