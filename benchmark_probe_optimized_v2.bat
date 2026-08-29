@echo off
setlocal EnableExtensions
pushd "%~dp0"

set "N=%~1"
set "LIMIT=%~2"
if not defined N set "N=20"
if not defined LIMIT set "LIMIT=60"

call build_probe_optimized_v2_fast_cpp.bat
if errorlevel 1 goto :error
call build_probe_optimized_v2_cpp.bat
if errorlevel 1 goto :error

echo Running speed-first v2: n=%N%, limit=%LIMIT%s
moto_probe_optimized_v2_fast.exe %N% %LIMIT% > "benchmark_v2_fast_n%N%.txt"
if errorlevel 1 goto :error
type "benchmark_v2_fast_n%N%.txt"

echo.
echo Running memory-first v2: n=%N%, limit=%LIMIT%s
moto_probe_optimized_v2.exe %N% %LIMIT% > "benchmark_v2_memory_n%N%.txt"
if errorlevel 1 goto :error
type "benchmark_v2_memory_n%N%.txt"

echo.
echo Wrote:
echo   benchmark_v2_fast_n%N%.txt
echo   benchmark_v2_memory_n%N%.txt
popd
exit /b 0

:error
echo Benchmark failed.
popd
exit /b 1
