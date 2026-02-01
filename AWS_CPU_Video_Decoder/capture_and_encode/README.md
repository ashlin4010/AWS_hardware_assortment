# AWS Video Decode #

This project is not finished. The goal is to decode and re-output the video captured from the AWS.

## Flashing ##
```
picotool.exe load bin/decode.elf -fx
```

## Building ##
1. Install the pico SDK via VS code (2.2.0)
2. Import the project
3. Compile

## Wiring ##

Please remember to use level shifters

* GPIO_02 - Vsync
* GPIO_03 - Hsync
* GPIO_04 - CC0
* GPIO_05 - CC1
* GPIO_06 - CC2
* GPIO_07 - CC3
* GPIO_08 - CC4
* GPIO_09 - CC5
* GPIO_10 - CC6
* GPIO_11 - AF
* GPIO_12 - CCLK

> CC = Character Code, AF = Alternate Font, CCLK = Character Clock