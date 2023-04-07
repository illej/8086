@echo off

setlocal EnableDelayedExpansion

call "%USERPROFILE%\code\msvc\setup.bat"

if exist computer_enhance (
    pushd computer_enhance
    call git pull
    popd
) else (
    call git clone https://github.com/cmuratori/computer_enhance.git
)
robocopy computer_enhance\perfaware\part1 . listing_*.asm > NUL

for %%f in (listing_*.asm) do (call nasm %%f)

cl.exe -nologo main.c
