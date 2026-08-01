@echo off
REM Host-side unit tests for hardware-independent logic (src/pixel_encode.h,
REM src/light_state.h, src/color.h). Builds and runs every test\test_* suite.
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

set FAILED=0
for /d %%T in (test\test_*) do call :run_suite %%T
echo.
if %FAILED% neq 0 (
  echo ==================== SOME SUITES FAILED ====================
  exit /b 1
)
echo ==================== ALL SUITES PASSED =====================
exit /b 0

:run_suite
set NAME=%~nx1
if not exist build\native\%NAME% mkdir build\native\%NAME%
cl /nologo /std:c++17 /EHsc /W3 /I src /I "%UNITY%" ^
   %1\test_main.cpp "%UNITY%\unity.c" ^
   /Fo:build\native\%NAME%\ /Fe:build\native\%NAME%.exe >build\native\%NAME%.log 2>&1
if errorlevel 1 (
  echo.
  echo --- BUILD FAILED: %NAME% ---
  type build\native\%NAME%.log
  set FAILED=1
  goto :eof
)
echo.
echo --- %NAME% ---
build\native\%NAME%.exe
if errorlevel 1 set FAILED=1
goto :eof
