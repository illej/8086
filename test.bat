@echo off

setlocal EnableDelayedExpansion

for %%f in (listing_*.asm) do (
    set fname=%%f
    set n=!fname:~8,2!

    call :test !n!
)

goto :end

:test
    set n=%1

    set input_binary=listing_%n%
    set input_source=listing_%n%.asm
    set output_binary=test_%n%
    set output_source=test_%n%.asm

    main.exe -f %output_source% %input_binary%
    call nasm %output_source%
    fc %input_binary% %output_binary% 1>NUL

    if %errorlevel% == 0 (echo %input_binary% .. OK) else (
        echo %input_binary% .. Failed
        echo ---------------------------
        type %output_source%
        echo ---------------------------
    )
    goto :eof

:end
