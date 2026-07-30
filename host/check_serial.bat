@echo off
setlocal
pushd %~dp0
if not exist .venv\Scripts\python.exe (
    call setup.bat
    if errorlevel 1 (
        popd
        exit /b 1
    )
)
set PYTHONPATH=%~dp0
.venv\Scripts\python.exe tools\check_serial.py %*
set RESULT=%ERRORLEVEL%
popd
exit /b %RESULT%
