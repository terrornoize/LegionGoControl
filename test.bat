@echo off
setlocal EnableExtensions
cd /d "%~dp0"

call :setup_x64 || exit /b 1

set "BUILD_DIR=%CD%\build"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%" || exit /b 1

rem Build only the platform-independent core and its tests.
cl /nologo /std:c++17 /EHsc /W4 /O2 /DNDEBUG /DUNICODE /D_UNICODE LegionGoCore.cpp LegionGoCoreTests.cpp /Fo"%BUILD_DIR%\\" /Fe"%BUILD_DIR%\LegionGoCoreTests.exe" /link /MACHINE:X64 /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1

"%BUILD_DIR%\LegionGoCoreTests.exe"
exit /b %ERRORLEVEL%

:setup_x64
if /I "%VSCMD_ARG_TGT_ARCH%"=="x64" where cl >nul 2>nul && exit /b 0
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio Build Tools with the C++ workload were not found.
    exit /b 1
)
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
    echo ERROR: Visual Studio x64 C++ tools were not found.
    exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b 1
where cl >nul 2>nul || exit /b 1
exit /b 0
