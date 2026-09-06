@echo off
rem Build seraph_drv.sys (kernel mode) + SeraphLoader.exe (usermode).
rem Requires WDK 10.0.26100 (km headers) + VS18 Insiders MSVC.
setlocal
set VCToolsInstallDir=C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Tools\MSVC\14.51.36231\
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

set SDK=10.0.26100.0
set KIT=C:\Program Files (x86)\Windows Kits\10
set KMINC=%KIT%\Include\%SDK%\km
set KMSHRD=%KIT%\Include\%SDK%\shared
set KMUCRT=%KIT%\Include\%SDK%\ucrt
set UMINC=%KIT%\Include\%SDK%\um
set UMSHRD=%KIT%\Include\%SDK%\shared
set UMUCRT=%KIT%\Include\%SDK%\ucrt
set UMLIB=%KIT%\Lib\%SDK%\um\x64;%KIT%\Lib\%SDK%\ucrt\x64
set KMLIB=%KIT%\Lib\%SDK%\km\x64
set MSVCINC=%VCToolsInstallDir%include
set MSVCLIB=%VCToolsInstallDir%lib\x64

set ROOT=C:\Users\ncomp\source\repos\BackupExternal\Seraph
set OUT=%ROOT%\build\driver
if not exist "%OUT%" mkdir "%OUT%"

echo === Compiling seraph_drv.sys (kernel) ===
cl /nologo /c /O2 /GS- /Gy /W3 /WX- /D_WIN64 /D_AMD64_ /DWIN32_LEAN_AND_MEAN /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR ^
   /std:c++17 /permissive- /Zp8 /kernel ^
   /I"%MSVCINC%" /I"%KMINC%" /I"%KMSHRD%" /I"%KMUCRT%" ^
   /Fo"%OUT%\seraph_drv.obj" ^
   "%ROOT%\driver\seraph_drv.cpp"
if errorlevel 1 exit /b 1

echo === Linking seraph_drv.sys ===
cl /nologo ^
   /Fe"%OUT%\seraph_drv.sys" ^
   "%OUT%\seraph_drv.obj" ^
   /link /NOLOGO /LIBPATH:"%KMLIB%" /LIBPATH:"%MSVCLIB%" ^
   ntoskrnl.lib hal.lib wdm.lib ^
   /DRIVER /ENTRY:DriverEntry /SUBSYSTEM:NATIVE ^
   /NODEFAULTLIB /MACHINE:X64 /OUT:"%OUT%\seraph_drv.sys"
if errorlevel 1 exit /b 1

echo === Compiling SeraphLoader.exe (usermode) ===
cl /nologo /c /O2 /W3 /WX- /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE ^
   /std:c++17 /permissive- ^
   /I"%MSVCINC%" /I"%UMINC%" /I"%UMSHRD%" /I"%UMUCRT%" ^
   /Fo"%OUT%\SeraphLoader.obj" ^
   "%ROOT%\loader\SeraphLoader.cpp"
if errorlevel 1 exit /b 1

echo === Linking SeraphLoader.exe ===
cl /nologo ^
   /Fe"%OUT%\SeraphLoader.exe" ^
   "%OUT%\SeraphLoader.obj" ^
   /link /NOLOGO /LIBPATH:"%UMLIB%" /LIBPATH:"%MSVCLIB%" ^
   kernel32.lib user32.lib advapi32.lib ^
   /SUBSYSTEM:CONSOLE /MACHINE:X64 /OUT:"%OUT%\SeraphLoader.exe"
if errorlevel 1 exit /b 1

echo.
echo === Driver + Loader built in:
echo %OUT%\seraph_drv.sys
echo %OUT%\SeraphLoader.exe
endlocal
