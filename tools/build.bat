@echo off
rem The Windows build. There is no second build system here: this is the same
rem file list and the same defines the Makefile has, written the one way MSVC
rem can be told them. A port that needed a build system would be a port that
rem changed the project; this one is a list of files. See D970.
setlocal enabledelayedexpansion

if "%~1"=="" ( set "OUT=build\win" ) else ( set "OUT=%~1" )
if not exist "%OUT%" mkdir "%OUT%"
if exist "%OUT%\said.txt" del "%OUT%\said.txt"

rem `/experimental:c11atomics` is what MSVC calls C11's own `<stdatomic.h>`,
rem which the machine uses for the one word a host may write while a program is
rem running. `_CRT_SECURE_NO_WARNINGS` turns off the advice to use Microsoft's
rem own spellings of `fopen` and `snprintf`, which are not C11 and are not what
rem this is written in.
set "WARN=/nologo /std:c11 /experimental:c11atomics /W4 /O2 /Iinclude /D_CRT_SECURE_NO_WARNINGS /DKEST_LIB_DIR=\"lib/kest/\""
rem The hosts are not held to C4456, which is what `-Wshadow` is called here,
rem for the reason the Makefile gives. See D973.
set "HOSTWARN=%WARN% /wd4456"
set "OBJ="
for %%f in (src\*.c) do (
    if /I not "%%~nxf"=="main.c" (
        cl %WARN% /c /Fo"%OUT%\%%~nf.obj" "%%f" >>"%OUT%\said.txt" 2>&1 || (type "%OUT%\said.txt" & exit /b 1)
        set "OBJ=!OBJ! "%OUT%\%%~nf.obj""
    )
)
lib /nologo /out:"%OUT%\kest.lib" !OBJ! || exit /b 1

cl %WARN% /c /Fo"%OUT%\main.obj" src\main.c >>"%OUT%\said.txt" 2>&1 || (type "%OUT%\said.txt" & exit /b 1)
cl /nologo /Fe"%OUT%\kest.exe" "%OUT%\main.obj" "%OUT%\kest.lib" || exit /b 1

cl %HOSTWARN% /c /Fo"%OUT%\embed.obj" examples\embed.c >>"%OUT%\said.txt" 2>&1 || (type "%OUT%\said.txt" & exit /b 1)
cl /nologo /Fe"%OUT%\embed.exe" "%OUT%\embed.obj" "%OUT%\kest.lib" || exit /b 1

cl %HOSTWARN% /c /Fo"%OUT%\engine.obj" examples\engine.c >>"%OUT%\said.txt" 2>&1 || (type "%OUT%\said.txt" & exit /b 1)
cl /nologo /Fe"%OUT%\engine.exe" "%OUT%\engine.obj" "%OUT%\kest.lib" || exit /b 1

rem Every warning at once rather than the first one. `/WX` stops at the first
rem file that has one, which is one round trip of the build for each, so the
rem warnings are collected and read at the end instead: the build still refuses
rem on any of them, and whoever is fixing them sees all of them. See D970.
findstr /C:"warning C" "%OUT%\said.txt" >nul
if not errorlevel 1 (
    echo.
    echo this build says:
    findstr /C:"warning C" "%OUT%\said.txt"
    exit /b 1
)

echo built %OUT%\kest.exe, %OUT%\embed.exe and %OUT%\engine.exe
endlocal
