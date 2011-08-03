@ECHO OFF

RMDIR /S /Q Debug
RMDIR /S /Q Release
RMDIR /S /Q Release26
RMDIR /S /Q ipch
DEL avs2pipe.sdf
DEL /AH avs2pipe.suo
DEL avs2pipe.vcxproj.user
