@ECHO OFF

call "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" x86

ECHO Starting AviSynth 2.5 Version Build
msbuild /property:Configuration=Release avs2pipe.vcxproj
ECHO.

ECHO Starting AviSynth 2.6 Version Build
msbuild /property:Configuration=Release26 avs2pipe.vcxproj
ECHO.
