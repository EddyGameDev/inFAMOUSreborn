@echo off
setlocal

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%

set "NINJA=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not defined PYTHON for /f "delims=" %%P in ('where python 2^>nul') do if not defined PYTHON set "PYTHON=%%P"

cmake -S . -B build-fast -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_C_COMPILER=clang-cl ^
  -DCMAKE_CXX_COMPILER=clang-cl ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DPS3RECOMP_DIR=../ps3recomp ^
  -DPython3_EXECUTABLE="%PYTHON%"
if errorlevel 1 (
  echo CMake configure failed.
  pause
  exit /b 1
)

cmake --build build-fast --parallel 4 > "%~dp0build.log" 2>&1
set "RC=%ERRORLEVEL%"
type "%~dp0build.log" | findstr /c:"error:" /c:"FAILED:" /c:"LNK1"
if not "%RC%"=="0" (
  echo.
  echo BUILD FAILED ^(code %RC%^). Full log: build.log
  echo If it says LNK1104 / cannot open inFamousRecomp.exe, close the game window first.
  pause
  exit /b %RC%
)
echo Build OK.
if not defined NOPAUSE pause
exit /b 0
