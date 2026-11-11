@echo off
setlocal
cd /d "%~dp0"

where cl >nul 2>&1
if %errorlevel%==0 goto compile

set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if exist "%VCVARS%" (
  call "%VCVARS%"
  goto compile
)

where g++ >nul 2>&1
if %errorlevel%==0 (
  g++ -std=c++17 -O2 -municode -mwindows -o TrayNotes.exe main.cpp -luser32 -lgdi32 -lshell32 -lcomctl32 -lole32 -luuid
  exit /b %errorlevel%
)

echo Need the Visual Studio 2022 developer prompt, or g++ on PATH.
exit /b 1

:compile
cl /nologo /O2 /EHsc /std:c++17 /Fe:TrayNotes.exe main.cpp /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib comctl32.lib ole32.lib uuid.lib dwmapi.lib uxtheme.lib
exit /b %errorlevel%
