@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
set VCToolsInstallDir=C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Tools\MSVC\14.51.36231\
"C:\Program Files\Microsoft Visual Studio\18\Insiders\Msbuild\Current\Bin\amd64\MSBuild.exe" "C:\Users\ncomp\source\repos\BackupExternal\Seraph\Seraph.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:VCToolsInstallDir="C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Tools\MSVC\14.51.36231\" /p:VCToolsVersion=14.51.36231 /m