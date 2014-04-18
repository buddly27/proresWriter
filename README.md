proresWriter
============

Prores encoder for Nuke (with ffmpeg)

I'm using the original ffmpegWriter.cpp provided by The Foundry.
You need to have FFmpeg 2.1.1 installed.


Compilation
============

Open the Makefile and replace the NDKDIR and FFMEGDIR variables with the correct paths. Then simply run 'make' in the main reporitory (or make -f Makefile.mac if you are using OSX).


Installation
============

Simply copy the proresWriter.so (or.dylib) generated in your ~/.nuke repository.



