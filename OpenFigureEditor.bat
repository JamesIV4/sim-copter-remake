@echo off
cd /d "%~dp0"
if not exist "Tools\.figure-editor-venv\Scripts\python.exe" (
    python -m venv Tools\.figure-editor-venv
    if errorlevel 1 goto :failed
)
"Tools\.figure-editor-venv\Scripts\python.exe" -c "import pyvista, pyvistaqt, PySide6" >nul 2>&1
if errorlevel 1 (
    "Tools\.figure-editor-venv\Scripts\python.exe" -m pip install -r Tools\figure-editor-requirements.txt
    if errorlevel 1 goto :failed
)
"Tools\.figure-editor-venv\Scripts\python.exe" Tools\FigureEditor.py %*
if errorlevel 1 pause
exit /b
:failed
echo Figure editor setup failed. See the error above.
pause
exit /b 1
