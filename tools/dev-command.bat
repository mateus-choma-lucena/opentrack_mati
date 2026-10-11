@echo off
setlocal
set "OTR_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%OTR_VSWHERE%" exit /b 1
for /f "usebackq delims=" %%i in (`"%OTR_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "OTR_VS=%%i"
if not defined OTR_VS exit /b 1
call "%OTR_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %ERRORLEVEL%
set "PATH=%OTR_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
%*
exit /b %ERRORLEVEL%
