@echo off
setlocal EnableExtensions
cd /d "%~dp0"

call :setup_x64 || exit /b 1

set "BUILD_DIR=%CD%\build"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%" || exit /b 1

set "CXXFLAGS=/nologo /std:c++17 /EHsc /W4 /O2 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE"

echo [1/3] Building LegionGoControl.exe (x64 Release)...
rc /nologo /fo "%BUILD_DIR%\LegionGoControl.res" "%CD%\LegionGoControl.rc"
if errorlevel 1 exit /b 1
cl %CXXFLAGS% LegionGoControl.cpp LegionGoCore.cpp LegionGoOverlay.cpp LegionGoPresentTrace.cpp LegionGoFrameLimiter.cpp "%BUILD_DIR%\LegionGoControl.res" /Fo"%BUILD_DIR%\\" /Fe"%CD%\LegionGoControl.exe" /link /MACHINE:X64 /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:"%CD%\app.manifest"
if errorlevel 1 exit /b 1

echo [2/3] Building LegionGoNativeWmiProbe.exe (x64 Release)...
cl %CXXFLAGS% LegionGoNativeWmiProbe.cpp /Fo"%BUILD_DIR%\\" /Fe"%CD%\LegionGoNativeWmiProbe.exe" /link /MACHINE:X64 /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1

echo [3/3] Building core tests...
cl %CXXFLAGS% LegionGoCore.cpp LegionGoCoreTests.cpp /Fo"%BUILD_DIR%\\" /Fe"%BUILD_DIR%\LegionGoCoreTests.exe" /link /MACHINE:X64 /SUBSYSTEM:CONSOLE
if errorlevel 1 exit /b 1

echo Build completed successfully.
exit /b 0

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
