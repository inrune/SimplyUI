@echo off
rem ============================================================================
rem  SimplyUI - one-click build. Double-click this file.
rem
rem  Builds with Visual Studio (2017 or newer, with "Desktop development with C++"):
rem    build\x64\simplyui.dll     64-bit DLL
rem    build\x86\simplyui32.dll   32-bit DLL
rem    build\x64\*.exe            the example programs
rem  and packs the release:
rem    release\SimplyUI-<version>.zip   (upload this to a GitHub release)
rem ============================================================================
setlocal EnableExtensions
set "ROOT=%~dp0"
cd /d "%ROOT%"

set "VERSION="
for /f "tokens=3" %%v in ('findstr /b /c:"#define SIMPLYUI_VERSION " include\simplyui.h') do set "VERSION=%%~v"
if not defined VERSION (
    echo Could not read the version from include\simplyui.h
    goto :failed
)
set "EXAMPLES=hello showcase file_hasher system_monitor todo timer"
set "CFLAGS=/nologo /MT /O2 /std:c++17 /EHsc /utf-8 /W3 /DNDEBUG"

echo.
echo SimplyUI %VERSION% - building with Visual Studio
echo.

rem ---- find Visual Studio ------------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :novs
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvarsall.bat > "%TEMP%\simplyui_vcvars.txt" 2>nul
set "VCVARS="
set /p VCVARS=<"%TEMP%\simplyui_vcvars.txt"
del "%TEMP%\simplyui_vcvars.txt" >nul 2>nul
if not defined VCVARS goto :novs
if not exist "%VCVARS%" goto :novs

rem ---- build ---------------------------------------------------------------------
call :build x64 simplyui.dll
if errorlevel 1 goto :failed
call :build x86 simplyui32.dll
if errorlevel 1 goto :failed

rem ---- package the release ----------------------------------------------------------
set "REL=release\SimplyUI-%VERSION%"
<nul set /p "=release\SimplyUI-%VERSION%.zip ... "
if exist "%REL%" rmdir /s /q "%REL%"
if exist "%REL%.zip" del /q "%REL%.zip"
mkdir "%REL%\examples"
copy /y include\simplyui.h "%REL%\" >nul
copy /y build\x64\simplyui.dll "%REL%\" >nul
copy /y build\x86\simplyui32.dll "%REL%\" >nul
copy /y LICENSE "%REL%\LICENSE.txt" >nul
copy /y build\x64\simplyui.dll "%REL%\examples\" >nul
for %%e in (%EXAMPLES%) do copy /y build\x64\%%e.exe "%REL%\examples\" >nul
set "README=%REL%\README.txt"
> "%README%" echo SimplyUI %VERSION% - a small immediate-mode GUI library for Windows tools.
>>"%README%" echo.
>>"%README%" echo HOW TO USE
>>"%README%" echo   1. Copy simplyui.h and simplyui.dll into your project folder.
>>"%README%" echo   2. #include "simplyui.h" and write your UI. Nothing to link, nothing to set up.
>>"%README%" echo   3. When you share your program, put simplyui.dll next to your .exe.
>>"%README%" echo   32-bit programs use simplyui32.dll instead.
>>"%README%" echo.
>>"%README%" echo FILES
>>"%README%" echo   simplyui.h       the header: the whole API, with comments
>>"%README%" echo   simplyui.dll     the library for 64-bit programs
>>"%README%" echo   simplyui32.dll   the library for 32-bit programs
>>"%README%" echo   examples\        demo programs, ready to run
>>"%README%" echo.
>>"%README%" echo Documentation and source code: the SimplyUI repository on GitHub.
>>"%README%" echo License: MIT, see LICENSE.txt.
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%REL%\*' -DestinationPath '%REL%.zip' -Force"
if exist "%REL%.zip" (echo ok) else (echo could not zip - zip the folder %REL% yourself)

echo.
echo Done!
echo   DLLs and examples:  build\x64  and  build\x86
echo   Release:            %REL%.zip  (and the folder %REL%)
echo.
if /i not "%~1"=="nopause" pause
exit /b 0

rem ---- build one architecture: call :build <x64|x86> <dll name> -------------------
:build
setlocal
call "%VCVARS%" %1 >nul 2>nul
cd /d "%ROOT%"
if not defined VCINSTALLDIR (echo Visual Studio could not be set up for %1. & exit /b 1)
if not exist build\%1 mkdir build\%1
set "OUT=%ROOT%build\%1"
set "LOG=%OUT%\%~n2.log"
set "RCFLAGS=/nologo"
if "%1"=="x86" set "RCFLAGS=/nologo /d SUI_32BIT"
<nul set /p "=[%1] %2 ... "
pushd src
rc %RCFLAGS% /fo "%OUT%\simplyui.res" simplyui.rc > "%LOG%" 2>&1
set "ERR=%errorlevel%"
popd
if not "%ERR%"=="0" goto :buildfailed
cl %CFLAGS% /bigobj /LD src\simplyui.cpp "%OUT%\simplyui.res" /Fo"%OUT%\simplyui.obj" /Fe"%OUT%\%2" /link /NOLOGO /INCREMENTAL:NO >> "%LOG%" 2>&1
if errorlevel 1 goto :buildfailed
echo ok
<nul set /p "=[%1] examples ... "
set "LOG=%OUT%\app.log"
pushd examples
rc /nologo /fo "%OUT%\app.res" app.rc > "%LOG%" 2>&1
set "ERR=%errorlevel%"
popd
if not "%ERR%"=="0" goto :buildfailed
for %%e in (%EXAMPLES%) do (
    cl %CFLAGS% /Iinclude examples\%%e\main.cpp "%OUT%\app.res" /Fo"%OUT%\%%e.obj" /Fe"%OUT%\%%e.exe" /link /NOLOGO /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup > "%OUT%\%%e.log" 2>&1
    if errorlevel 1 (
        echo FAILED: %%e
        type "%OUT%\%%e.log"
        exit /b 1
    )
)
echo ok
endlocal
exit /b 0

:buildfailed
echo FAILED
type "%LOG%"
exit /b 1

:novs
echo Visual Studio with C++ was not found.
echo.
echo Install Visual Studio 2017 or newer - the free Community edition is fine -
echo with the "Desktop development with C++" workload, then run build.bat again:
echo https://visualstudio.microsoft.com/
echo.
if /i not "%~1"=="nopause" pause
exit /b 1

:failed
echo.
echo Build failed - see the messages above.
echo.
if /i not "%~1"=="nopause" pause
exit /b 1
