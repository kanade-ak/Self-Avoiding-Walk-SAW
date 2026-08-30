@echo off
setlocal
pushd "%~dp0..\.."
if not exist "build" mkdir "build"

call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :error

cl.exe /nologo /O2 /GL /EHsc /std:c++20 /utf-8 src\legacy\moto_probe_optimized.cpp /Fe:build\moto_probe_optimized.exe /Fo:build\moto_probe_optimized.obj /link /LTCG
if errorlevel 1 goto :error
del /q build\moto_probe_optimized.obj >nul 2>&1

echo Built: %CD%\build\moto_probe_optimized.exe
popd
exit /b 0

:error
echo Build failed.
popd
exit /b 1
