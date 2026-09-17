@echo off
rem The Windows build. There is no second build system here: this is the same
rem file list and the same defines the Makefile has, written the one way MSVC
rem can be told them. A port that needed a build system would be a port that
rem changed the project; this one is a list of files. See D970.
setlocal enabledelayedexpansion

if "%~1"=="" ( set "OUT=build\win" ) else ( set "OUT=%~1" )
if not exist "%OUT%" mkdir "%OUT%"

set "WARN=/nologo /std:c11 /W4 /WX /O2 /Iinclude /DKEST_LIB_DIR=\"lib/kest/\""
set "OBJ="
for %%f in (src\*.c) do (
    if /I not "%%~nxf"=="main.c" (
        cl %WARN% /c /Fo"%OUT%\%%~nf.obj" "%%f" || exit /b 1
        set "OBJ=!OBJ! "%OUT%\%%~nf.obj""
    )
)
lib /nologo /out:"%OUT%\kest.lib" !OBJ! || exit /b 1

cl %WARN% /c /Fo"%OUT%\main.obj" src\main.c || exit /b 1
cl /nologo /Fe"%OUT%\kest.exe" "%OUT%\main.obj" "%OUT%\kest.lib" || exit /b 1

cl %WARN% /c /Fo"%OUT%\embed.obj" examples\embed.c || exit /b 1
cl /nologo /Fe"%OUT%\embed.exe" "%OUT%\embed.obj" "%OUT%\kest.lib" || exit /b 1

cl %WARN% /c /Fo"%OUT%\engine.obj" examples\engine.c || exit /b 1
cl /nologo /Fe"%OUT%\engine.exe" "%OUT%\engine.obj" "%OUT%\kest.lib" || exit /b 1

echo built %OUT%\kest.exe, %OUT%\embed.exe and %OUT%\engine.exe
endlocal
