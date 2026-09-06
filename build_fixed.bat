@echo off
setlocal

:: Use MSVC 14.30.30705 toolset (which has full includes/lib/ml64)
set MSVC_VER=14.30.30705
set MSVC_BASE=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\%MSVC_VER%

:: Required by MSBuild CppBuild.targets
set VCToolsInstallDir=%MSVC_BASE%\
set VCToolsVersion=%MSVC_VER%
set VCINSTALLDIR=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\

:: Compiler, linker, assembler
set PATH=%MSVC_BASE%\bin\HostX64\x64;%PATH%

:: Windows SDK (10.0.26100.0)
set WKSD_VER=10.0.26100.0
set WKSD_INC=C:\Program Files (x86)\Windows Kits\10\Include\%WKSD_VER%
set WKSD_LIB=C:\Program Files (x86)\Windows Kits\10\Lib\%WKSD_VER%

:: Include paths (compiler needs these on INCLUDE)
set INCLUDE=%MSVC_BASE%\include;%WKSD_INC%\um;%WKSD_INC%\ucrt;%WKSD_INC%\shared;%WKSD_INC%\winrt;%INCLUDE%

:: Lib paths
set LIB=%MSVC_BASE%\lib\x64;%WKSD_LIB%\um\x64;%WKSD_LIB%\ucrt\x64;%LIB%

set MSBUILD=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe

echo Building Seraph Release x64...
"%MSBUILD%" Seraph.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal /m:1

endlocal
