@echo off
set VCToolsInstallDir=C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Tools\MSVC\14.51.36231\
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
"C:\Program Files\Microsoft Visual Studio\18\Insiders\Msbuild\Current\Bin\amd64\MSBuild.exe" "C:\Users\ncomp\source\repos\BackupExternal\Seraph\Seraph.vcxproj" /p:Configuration=Release /p:Platform=x64 /m