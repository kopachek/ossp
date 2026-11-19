@echo off
if not exist build mkdir build
cl /nologo /W4 /O2 /Fo:build\ /Fe:build\counter.exe src\counter.c
