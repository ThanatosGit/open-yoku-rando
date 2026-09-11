@echo off
rem Builds build\open_yoku_rando.dll and build\xinput9_1_0.dll with the newest Visual Studio that has the C++ tools.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_vs
set "VSROOT="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT goto :no_vs

rem vcvars prints a harmless "vswhere.exe not found" when vswhere is not on PATH
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || goto :fail

rem Use the CMake and Ninja that ship with Visual Studio; other CMakes on PATH (e.g. devkitPro's MSYS one) don't know MSVC.
set "CMAKE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

pushd "%~dp0"
"%CMAKE%" -S . -B build -G Ninja "-DCMAKE_MAKE_PROGRAM=%NINJA%" -DCMAKE_BUILD_TYPE=Release || goto :fail_pop
"%CMAKE%" --build build || goto :fail_pop
popd
exit /b 0

:no_vs
echo Visual Studio with the "Desktop development with C++" workload was not found.
exit /b 1
:fail_pop
popd
:fail
exit /b 1
