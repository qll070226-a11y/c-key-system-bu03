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
.venv\Scripts\python.exe -m c_key_debugger %*
set RESULT=%ERRORLEVEL%
popd
exit /b %RESULT%
