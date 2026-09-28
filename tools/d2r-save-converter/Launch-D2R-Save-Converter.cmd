@echo off
setlocal
where node.exe >nul 2>nul
if errorlevel 1 (
  echo Node.js is required by this development build.
  echo The public release will include a standalone executable.
  pause
  exit /b 1
)
node "%~dp0src\cli-entry.mjs"
echo.
pause
