@echo off
setlocal EnableExtensions
pushd "%~dp0..\.."
if not exist "build" mkdir "build"
if not exist "benchmarks\audit_logs\build_artifacts" mkdir "benchmarks\audit_logs\build_artifacts"
if not exist "benchmarks\results" mkdir "benchmarks\results"

where cl.exe >nul 2>&1
if not errorlevel 1 goto :train_single
call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :error

:train_single
cl.exe /nologo /O2 /GL /EHsc /std:c++20 /utf-8 /W4 /permissive- /DNDEBUG src\moto_probe_mph_inplace.cpp /Fe:build\moto_probe_mph_inplace_pgo_training.exe /Fo:build\moto_probe_mph_inplace_pgo_training.obj /link /LTCG /GENPROFILE:PGD=benchmarks\audit_logs\build_artifacts\moto_probe_mph_inplace_single.pgd
if errorlevel 1 goto :error
pushd "benchmarks\audit_logs\build_artifacts"
..\..\..\build\moto_probe_mph_inplace_pgo_training.exe 16 120 > ..\..\results\benchmark_mph_inplace_pgo_single_training_n16.txt
popd
if errorlevel 1 goto :error
del /q build\moto_probe_mph_inplace_pgo_training.obj >nul 2>&1
cl.exe /nologo /O2 /GL /EHsc /std:c++20 /utf-8 /W4 /permissive- /DNDEBUG src\moto_probe_mph_inplace.cpp /Fe:build\moto_probe_mph_inplace_pgo.exe /Fo:build\moto_probe_mph_inplace_pgo.obj /link /LTCG /USEPROFILE:PGD=benchmarks\audit_logs\build_artifacts\moto_probe_mph_inplace_single.pgd
if errorlevel 1 goto :error
del /q build\moto_probe_mph_inplace_pgo.obj >nul 2>&1

echo Built single-thread PGO executable.

cl.exe /nologo /O2 /GL /EHsc /std:c++20 /utf-8 /W4 /permissive- /DNDEBUG /openmp src\moto_probe_mph_inplace.cpp /Fe:build\moto_probe_mph_inplace_parallel_pgo_training.exe /Fo:build\moto_probe_mph_inplace_parallel_pgo_training.obj /link /LTCG /GENPROFILE:PGD=benchmarks\audit_logs\build_artifacts\moto_probe_mph_inplace_parallel.pgd
if errorlevel 1 goto :parallel_unavailable
set "OMP_NUM_THREADS=16"
pushd "benchmarks\audit_logs\build_artifacts"
..\..\..\build\moto_probe_mph_inplace_parallel_pgo_training.exe 16 120 > ..\..\results\benchmark_mph_inplace_pgo_parallel_training_n16.txt
popd
if errorlevel 1 goto :error
del /q build\moto_probe_mph_inplace_parallel_pgo_training.obj >nul 2>&1
cl.exe /nologo /O2 /GL /EHsc /std:c++20 /utf-8 /W4 /permissive- /DNDEBUG /openmp src\moto_probe_mph_inplace.cpp /Fe:build\moto_probe_mph_inplace_parallel_pgo.exe /Fo:build\moto_probe_mph_inplace_parallel_pgo.obj /link /LTCG /USEPROFILE:PGD=benchmarks\audit_logs\build_artifacts\moto_probe_mph_inplace_parallel.pgd
if errorlevel 1 goto :parallel_unavailable
del /q build\moto_probe_mph_inplace_parallel_pgo.obj >nul 2>&1

echo Built parallel PGO executable.
popd
exit /b 0

:parallel_unavailable
echo Parallel PGO build was unavailable; single-thread PGO build remains.
popd
exit /b 0

:error
echo PGO experiment failed. Intermediate profile files were retained.
popd
exit /b 1
