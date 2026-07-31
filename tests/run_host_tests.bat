@echo off
setlocal

set "ROOT=%~dp0.."
set "OUT=%~dp0build"

if not exist "%OUT%" mkdir "%OUT%"

gcc -std=c17 -Wall -Wextra -Werror -pedantic ^
    -I"%ROOT%\components\c_key_core\include" ^
    "%ROOT%\components\c_key_core\c_key_core.c" ^
    "%ROOT%\components\c_key_core\c_key_pipeline.c" ^
    "%ROOT%\components\c_key_core\c_key_display.c" ^
    "%ROOT%\components\c_key_core\bu03_uart2.c" ^
    "%ROOT%\components\c_key_core\bu03_twr_usb.c" ^
    "%ROOT%\components\c_key_core\bu04_pdoa.c" ^
    "%ROOT%\components\c_key_core\c_key_bu03_bridge.c" ^
    "%ROOT%\components\c_key_core\c_key_telemetry.c" ^
    "%~dp0test_core.c" ^
    "%~dp0test_pipeline.c" ^
    "%~dp0test_display.c" ^
    "%~dp0test_bu03_uart2.c" ^
    "%~dp0test_bu03_twr_usb.c" ^
    "%~dp0test_bu04_pdoa.c" ^
    "%~dp0test_bu03_bridge.c" ^
    "%~dp0test_contest_scenario.c" ^
    "%~dp0test_telemetry.c" ^
    -lm -o "%OUT%\test_core.exe"
if errorlevel 1 exit /b 1

"%OUT%\test_core.exe"
exit /b %ERRORLEVEL%
