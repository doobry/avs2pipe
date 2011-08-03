avs2pipe is a tool to output y4m video, wav audio or dump some info about the
input avs clip.

It simply takes a path to an avs script that returns a clip with audio and/or
video and outputs it as a y4m (--video) or wav (--audio) stream to stdout. It
can also generate information about the clip (--info) in an easily parsable
format for gui's.

The advantage of avs2pipe over other solutions, other than it doing three
things in one, is that it links directly with avisynth.dll via the C interface,
so it compiles with both Visual Studio and MinGW and when used with wine in
linux simply requires that the avisynth dll's are in PATH, so no need to have
vfw configured correctly. Also as it uses WAVE_FORMAT_EXTENSIBLE for wav output
and directly takes data from avisynth it will output exactly the same data as
avisynth uses internally ie AVS_SAMPLE_INT* or AVS_SAMPLE_FLOAT.

Source is provided in the src folder and is released under the GPL-3.0.
Projects and batch files to build the code on Windows are provided in
src\vs2010 folder for Visual Studio 2010 Express and
src\mingw for MinGWon Windows.

The YUV4MPEG2 output is inspired by Avs2YUV by Loren Merritt, it is basically
the same method, converted from C++ to C, so it should provide identical output.

The WAV output is coded from scratch using specs from "the internet" and so
could be full of problems, altho I have not found any in testing yet.


Examples:

avs2pipe --info input.avs
avs2pipe --info input.avs > info.txt

avs2pipe --video input.avs | x264 --stdin y4m - --output video.aac
avs2pipe --audio input.avs | neroAacEnc -q 0.25 -if - -of audio.mp4

avs2pipe --audio input.avs > output.wav


Included Binaries:

avs2pipe_gcc.exe - compiled with MinGW gcc 4.5.2 on Windows
avs2pipe_vs.exe  - compiled with Visual Studio 2010 Express cl 16.00.30319.01


Download:

http://doobrymedia.com/avs2pipe-0.0.1.zip


Changes:

Pre-Alpha - Initial Release on doom10.org
Version 0.0.1 - Removed Progress Output for "Speed"


Forum Links:

Original - http://doom10.org/index.php?topic=759
Doom9 Continuation - http://forum.doom9.org/showthread.php?t=160383

