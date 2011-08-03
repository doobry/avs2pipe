@ECHO OFF

SET PATH=C:\MinGW\bin

pushd ..

gcc -v -O2 avs2pipe.c common.c wave.c avisynth.lib -o ..\avs2pipe_gcc.exe
strip ..\avs2pipe_gcc.exe

popd

PAUSE
