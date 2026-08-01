@echo off
REM Host-side unit tests for hardware-independent logic (src/pixel_encode.h, src/color.h).
REM
REM PlatformIO's `platform = native` hard-codes a gcc toolchain, which isn't installed
REM on this machine -- MSVC Build Tools are. This script compiles the same test files
REM against Unity with cl.exe instead. Run it from the repo root:
REM
REM     scripts\run-native-tests.bat
REM
setlocal
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat
set UNITY=.pio\libdeps\native\Unity\src

if not exist "%VCVARS%" (
  echo ERROR: MSVC Build Tools not found at "%VCVARS%"
  exit /b 1
)
if not exist "%UNITY%\unity.c" (
  echo ERROR: Unity not found. Run `pio test -e native` once to download it.
  exit /b 1
)

call "%VCVARS%" >nul
if not exist build\native mkdir build\native

cl /nologo /std:c++17 /EHsc /W3 ^
   /I src /I "%UNITY%" ^
   test\test_pixel_encode\test_main.cpp "%UNITY%\unity.c" ^
   /Fo:build\native\ /Fe:build\native\test_pixel_encode.exe
if errorlevel 1 exit /b 1

echo.
build\native\test_pixel_encode.exe
exit /b %errorlevel%
