@echo off
setlocal
pushd %~dp0

if not exist .venv\Scripts\python.exe (
    py -3.13 -m venv .venv
    if errorlevel 1 py -3 -m venv .venv
    if errorlevel 1 (
        echo Failed to create Python virtual environment.
        popd
        exit /b 1
    )
)

.venv\Scripts\python.exe -m pip install --upgrade pip
if errorlevel 1 goto :fail
.venv\Scripts\python.exe -m pip install -r requirements.txt
if errorlevel 1 goto :fail

echo Host environment is ready.
popd
exit /b 0

:fail
echo Host environment setup failed.
popd
exit /b 1
