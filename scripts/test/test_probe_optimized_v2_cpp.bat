@echo off
setlocal EnableExtensions
pushd "%~dp0..\.."
if not exist "build" mkdir "build"

call scripts\build\build_probe_optimized_v2_cpp.bat
if errorlevel 1 goto :error
call scripts\build\build_probe_optimized_v2_fast_cpp.bat
if errorlevel 1 goto :error

where cl.exe >nul 2>&1
if not errorlevel 1 goto :compile_tests

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALL="
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VSINSTALL=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
if not defined VSINSTALL if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" set "VSINSTALL=%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
if not defined VSINSTALL if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VSINSTALL=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if not defined VSINSTALL if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VSINSTALL=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
if not defined VSINSTALL goto :error
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :error

:compile_tests
cl.exe /nologo /O2 /EHsc /std:c++20 /utf-8 /W4 /permissive- /DNDEBUG tests\moto_probe_optimized_v2_tests.cpp /Fe:build\moto_probe_optimized_v2_tests.exe /Fo:build\moto_probe_optimized_v2_tests.obj
if errorlevel 1 goto :error
cl.exe /nologo /O2 /EHsc /std:c++20 /utf-8 /W4 /permissive- /DNDEBUG tests\moto_probe_optimized_v2_fast_tests.cpp /Fe:build\moto_probe_optimized_v2_fast_tests.exe /Fo:build\moto_probe_optimized_v2_fast_tests.obj
if errorlevel 1 goto :error

build\moto_probe_optimized_v2_tests.exe
if errorlevel 1 goto :error
build\moto_probe_optimized_v2_fast_tests.exe
if errorlevel 1 goto :error
del /q build\moto_probe_optimized_v2_tests.obj build\moto_probe_optimized_v2_fast_tests.obj >nul 2>&1
del /q build\moto_probe_optimized_v2_tests.exe build\moto_probe_optimized_v2_fast_tests.exe >nul 2>&1

echo All v2 builds and tests passed.
popd
exit /b 0

:error
echo V2 build or test failed.
popd
exit /b 1
