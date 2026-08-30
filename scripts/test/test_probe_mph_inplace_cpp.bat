@echo off
setlocal EnableExtensions
pushd "%~dp0..\.."
if not exist "build" mkdir "build"

call scripts\build\build_probe_mph_inplace_cpp.bat
if errorlevel 1 goto :error

where cl.exe >nul 2>&1
if errorlevel 1 call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :error

cl.exe /nologo /O2 /EHsc /std:c++20 /utf-8 /W4 /permissive- /DNDEBUG tests\moto_probe_mph_inplace_tests.cpp /Fe:build\moto_probe_mph_inplace_tests.exe /Fo:build\moto_probe_mph_inplace_tests.obj
if errorlevel 1 goto :error
build\moto_probe_mph_inplace_tests.exe
if errorlevel 1 goto :error
del /q build\moto_probe_mph_inplace_tests.exe build\moto_probe_mph_inplace_tests.obj >nul 2>&1

echo Minimal-perfect-hash in-place build and tests passed.
popd
exit /b 0

:error
echo Minimal-perfect-hash in-place build or test failed.
popd
exit /b 1
