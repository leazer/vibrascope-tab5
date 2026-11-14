@echo off
rem Build and run the MSVC host regression (probe_protocol + probe_link + app domain).
call "D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || (echo vcvars failed & exit /b 1)
set "PATH=E:\Toolchain\espressif-tools-v5.4.2\tools\cmake\3.30.2\bin;E:\Toolchain\espressif-tools-v5.4.2\tools\ninja\1.12.1;%PATH%"
cd /d "%~dp0"
if not exist build-host cmake -S . -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build-host || exit /b 1
build-host\vhm_host_tests.exe
