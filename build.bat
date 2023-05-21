@echo off

setlocal EnableDelayedExpansion

call "%USERPROFILE%\code\msvc\setup.bat"

for %%f in (listing_*.asm) do (call nasm %%f)

cl.exe -nologo main.c

ctags -R --langmap=c:.c.h --languages=c .
