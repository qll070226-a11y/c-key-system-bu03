@echo off
setlocal

if "%~1"=="" (
    echo Usage: flash_monitor_idf.bat COM13
    exit /b 2
)

set IDF_ROOT=D:\Espressif\Espressif\frameworks\esp-idf-v5.5.4
set IDF_PYTHON_ENV_PATH=D:\Espressif\Espressif\python_env\idf5.5_py3.11_env

call %IDF_ROOT%\export.bat
if errorlevel 1 exit /b 1

pushd %~dp0
idf.py -p %~1 flash monitor
set RESULT=%ERRORLEVEL%
popd
exit /b %RESULT%
