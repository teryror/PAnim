@echo off

IF "%1" NEQ "" GOTO Compile
echo Usage: %0 <name>
echo where scene_<name>.c should be a file in .\src\
GOTO End

:Compile
pushd .
cd bin

SET SourceFile=..\src\scene_%1.c
SET WarningsFlags=/W3 /WX /D_CRT_SECURE_NO_WARNINGS
SET CompilerFlags=/nologo /Fepanim.exe /I..\include /O2 /Zi %WarningsFlags%

SET FFmpegLibs=avcodec.lib avformat.lib avutil.lib swscale.lib
SET SdlLibs=x64\SDL2.lib x64\SDL2main.lib x64\SDL2_image.lib x64\SDL2_ttf.lib
SET LinkerFlags=/link /libpath:..\lib %FFmpegLibs% %SdlLibs%

cl %SourceFile% %CompilerFlags% %LinkerFlags%

popd

:End