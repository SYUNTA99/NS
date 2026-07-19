@echo off
::============================================================================
:: @regen_project.cmd
:: Regenerate the Visual Studio solution.
::
:: Usage: tools\@regen_project.cmd
::
:: NOTE: this header must be ASCII. It is parsed before chcp 65001 (line below)
::       takes effect, and cmd.exe misparses UTF-8 multibyte here
::       (a trailing byte eats the CR and a comment fragment gets executed).
::============================================================================
chcp 65001 >nul
call "%~dp0_common.cmd" :generate_project
pause
