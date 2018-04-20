@echo off

pushd .
cd bin

SET WarningsFlags=/W3 /WX /D_CRT_SECURE_NO_WARNINGS
SET CompilerFlags=/nologo /I..\include /O2 /Zi %WarningsFlags%

SET FFMpegLibs=avcodec.lib avutil.lib swscale.lib
SET SdlLibs=x64\SDL2.lib x64\SDL2main.lib x64\SDL2_image.lib x64\SDL2_ttf.lib
SET LinkerFlags=/link /libpath:..\lib %FFMpegLibs% %SdlLibs%

cl ..\src\panim.c %CompilerFlags% %LinkerFlags%

popd