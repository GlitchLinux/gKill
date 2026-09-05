#!/bin/bash
cd /tmp/gkill-build/
x86_64-w64-mingw32-windres resources.rc -o resources.o
x86_64-w64-mingw32-gcc -O2 -o gkill.exe gkill.c resources.o \
    -lgdi32 -lcomctl32 -lpsapi -lshell32 -luxtheme -mwindows -municode -static
x86_64-w64-mingw32-strip gkill.exe
ls -lh gkill.exe
file gkill.exe
