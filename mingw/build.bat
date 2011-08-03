@ECHO OFF

SET PATH=C:\PROGRA~2\Qt\qtcreator-2.1.0\mingw\bin

ECHO Starting AviSynth 2.5 Version Build
mingw32-make
ECHO.

ECHO Starting AviSynth 2.6 Version Build
mingw32-make "VERSION=26"
ECHO.
