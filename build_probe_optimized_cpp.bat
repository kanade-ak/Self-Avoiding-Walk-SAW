@echo off
setlocal
pushd "%~dp0"

call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :error

cl.exe /nologo /O2 /GL /EHsc /std:c++20 /utf-8 moto_probe_optimized.cpp /Fe:moto_probe_optimized.exe /link /LTCG
if errorlevel 1 goto :error
del /q moto_probe_optimized.obj >nul 2>&1

echo Built: %CD%\moto_probe_optimized.exe
popd
exit /b 0

:error
echo Build failed.
popd
exit /b 1
