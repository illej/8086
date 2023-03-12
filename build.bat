@echo off

call nasm listing_37.asm
call nasm listing_38.asm

cl main.c
