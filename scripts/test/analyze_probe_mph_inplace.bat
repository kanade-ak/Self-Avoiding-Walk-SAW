@echo off
setlocal
pushd "%~dp0..\.."
if not exist "benchmarks\audit_logs\build_artifacts" mkdir "benchmarks\audit_logs\build_artifacts"
call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :error
pushd "benchmarks\audit_logs\build_artifacts"
cl.exe /nologo /std:c++20 /utf-8 /permissive- /W4 /analyze /EHsc /O2 /DNDEBUG /c ..\..\..\src\moto_probe_mph_inplace.cpp /Fo:moto_probe_mph_inplace_analyze.obj
set "ANALYZE_ERROR=%ERRORLEVEL%"
popd
if not "%ANALYZE_ERROR%"=="0" goto :error
echo Static analysis completed. Analysis object retained.
popd
exit /b 0
:error
echo Static analysis failed.
popd
exit /b 1
