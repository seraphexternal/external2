@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
msbuild "C:\Users\ncomp\source\repos\BackupExternal\Seraph\Seraph.vcxproj" /p:Configuration=Release /p:Platform=x64 /m