@echo off
rem The Windows archive, which is the same shape as the one the Makefile writes
rem on the other platform: a binary, a header, a library, the standard library
rem beside them, the sources because the runtime is vendorable, the documents,
rem the licence, and a manifest saying what is in it. There is no second build
rem system here and this is not one: it is a list of copies and a zip. See
rem D1000.
setlocal enabledelayedexpansion

if "%~1"=="" ( set "OUT=build\win" ) else ( set "OUT=%~1" )
if not exist "%OUT%\kest.exe" (
    echo package: %OUT%\kest.exe is not built; run tools\build.bat first
    exit /b 1
)

rem The version out of the binary rather than out of a second copy of the
rem number. An archive that says it is something the thing inside it is not is
rem the one mistake a name written by hand makes. See D998.
for /f "tokens=2" %%v in ('"%OUT%\kest.exe" --version') do set "SAID=%%v"
set "VERSION=%SAID:,=%"
set "NAME=kest-%VERSION%-windows-x86_64"
set "DIR=build\%NAME%"

if exist "%DIR%" rmdir /s /q "%DIR%"
mkdir "%DIR%\bin"
mkdir "%DIR%\include"
mkdir "%DIR%\lib\kest\std"
mkdir "%DIR%\src"
mkdir "%DIR%\editors"

copy /y "%OUT%\kest.exe" "%DIR%\bin\" >nul || exit /b 1
copy /y include\kest.h "%DIR%\include\" >nul || exit /b 1
copy /y "%OUT%\kest.lib" "%DIR%\lib\" >nul || exit /b 1
copy /y lib\std\*.kest "%DIR%\lib\kest\std\" >nul || exit /b 1
copy /y src\*.c "%DIR%\src\" >nul || exit /b 1
copy /y src\*.h "%DIR%\src\" >nul || exit /b 1
xcopy /e /i /q editors\vscode "%DIR%\editors\vscode" >nul || exit /b 1
xcopy /e /i /q docs "%DIR%\docs" >nul || exit /b 1
copy /y README.md "%DIR%\" >nul || exit /b 1
copy /y CHANGELOG.md "%DIR%\" >nul || exit /b 1
copy /y LICENSE "%DIR%\" >nul || exit /b 1

"%OUT%\kest.exe" --version > "%DIR%\VERSION"
echo windows-x86_64>> "%DIR%\VERSION"
echo unpack it anywhere; bin\kest.exe finds lib\kest beside it>> "%DIR%\VERSION"
echo what it is and how to build a host against it: README.md>> "%DIR%\VERSION"
echo what changed: CHANGELOG.md. what it means: docs\language.md>> "%DIR%\VERSION"
echo the licence every file of it is under: LICENSE>> "%DIR%\VERSION"

rem The one thing here that is not a copy. Written the way the other platform
rem writes it -- the hash, two spaces, the name -- so that one reader reads
rem both.
powershell -NoProfile -Command ^
  "Compress-Archive -Force -Path 'build\%NAME%' -DestinationPath 'build\%NAME%.zip'" ^
  || exit /b 1
powershell -NoProfile -Command ^
  "$h = (Get-FileHash -Algorithm SHA256 'build\%NAME%.zip').Hash.ToLower();" ^
  "Set-Content -NoNewline -Encoding ascii 'build\%NAME%.zip.sha256' ($h + '  %NAME%.zip' + [char]10)" ^
  || exit /b 1

echo wrote build\%NAME%.zip and its checksum
endlocal
exit /b 0
