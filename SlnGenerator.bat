@echo off
setlocal
pushd "%~dp0"

"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0Setup\SetupFbxSdk.ps1"
if errorlevel 1 goto :failed

"%~dp0Setup\premake5.exe" --file=Setup/premake5.lua vs2026 %*
if errorlevel 1 goto :failed

popd
echo Project setup completed.
if not defined CI pause
exit /b 0

:failed
set "SetupResult=%errorlevel%"
echo Project setup failed. See the error above.
popd
if not defined CI pause
exit /b %SetupResult%
