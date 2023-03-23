@echo off

setlocal EnableDelayedExpansion

call "%USERPROFILE%\code\msvc\setup.bat"

call nasm listing_37.asm
call nasm listing_38.asm
call nasm listing_39.asm
call nasm listing_40.asm

cl.exe -nologo main.c
