@echo off
setlocal
pushd "%~dp0"
call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :error
cl.exe /nologo /std:c++20 /utf-8 /permissive- /W4 /analyze /EHsc /O2 /DNDEBUG /c moto_probe_mph_inplace.cpp /Fo:moto_probe_mph_inplace_analyze.obj
if errorlevel 1 goto :error
echo Static analysis completed. Analysis object retained.
popd
exit /b 0
:error
echo Static analysis failed.
popd
exit /b 1
