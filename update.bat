@echo off

if exist computer_enhance (
    pushd computer_enhance
    call git pull
    popd
) else (
    call git clone https://github.com/cmuratori/computer_enhance.git
)

robocopy computer_enhance\perfaware\part1 asm listing_*.asm > NUL
