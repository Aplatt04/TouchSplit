@echo off
rem Build on Windows with MinGW-w64 (e.g. w64devkit or MSYS2) on PATH.
if not exist build mkdir build
windres -I res res\touchsplit.rc -O coff -o build\res.o || goto :err
gcc -O2 -Wall -municode -mwindows -static src\touchsplit.c build\res.o -o TouchSplit.exe -luser32 -lshell32 -lgdi32 -ladvapi32 || goto :err
echo Built TouchSplit.exe
exit /b 0
:err
echo Build failed.
exit /b 1
