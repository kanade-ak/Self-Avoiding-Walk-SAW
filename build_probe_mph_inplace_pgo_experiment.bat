@echo off
setlocal EnableExtensions
pushd "%~dp0"

where cl.exe >nul 2>&1
if not errorlevel 1 goto :train_single
call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :error

:train_single
cl.exe /nologo /O2 /GL /EHsc /std:c++20 /utf-8 /W4 /permissive- /DNDEBUG moto_probe_mph_inplace.cpp /Fe:moto_probe_mph_inplace_pgo_training.exe /link /LTCG /GENPROFILE:PGD=moto_probe_mph_inplace_single.pgd
if errorlevel 1 goto :error
moto_probe_mph_inplace_pgo_training.exe 16 120 > benchmark_mph_inplace_pgo_single_training_n16.txt
if errorlevel 1 goto :error
cl.exe /nologo /O2 /GL /EHsc /std:c++20 /utf-8 /W4 /permissive- /DNDEBUG moto_probe_mph_inplace.cpp /Fe:moto_probe_mph_inplace_pgo.exe /link /LTCG /USEPROFILE:PGD=moto_probe_mph_inplace_single.pgd
if errorlevel 1 goto :error

echo Built single-thread PGO executable.

cl.exe /nologo /O2 /GL /EHsc /std:c++20 /utf-8 /W4 /permissive- /DNDEBUG /openmp moto_probe_mph_inplace.cpp /Fe:moto_probe_mph_inplace_parallel_pgo_training.exe /link /LTCG /GENPROFILE:PGD=moto_probe_mph_inplace_parallel.pgd
if errorlevel 1 goto :parallel_unavailable
set "OMP_NUM_THREADS=16"
moto_probe_mph_inplace_parallel_pgo_training.exe 16 120 > benchmark_mph_inplace_pgo_parallel_training_n16.txt
if errorlevel 1 goto :error
cl.exe /nologo /O2 /GL /EHsc /std:c++20 /utf-8 /W4 /permissive- /DNDEBUG /openmp moto_probe_mph_inplace.cpp /Fe:moto_probe_mph_inplace_parallel_pgo.exe /link /LTCG /USEPROFILE:PGD=moto_probe_mph_inplace_parallel.pgd
if errorlevel 1 goto :parallel_unavailable

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
