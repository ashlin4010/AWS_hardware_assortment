# Video Display Adaptor #

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
* GPIO_04 - CC0 (Character Code 0)
* GPIO_05 - CC1 (Character Code 1)
* GPIO_06 - CC2 (Character Code 2)
* GPIO_07 - CC3 (Character Code 3)
* GPIO_08 - CC4 (Character Code 4)
* GPIO_09 - CC5 (Character Code 5)
* GPIO_10 - CC6 (Character Code 6)
* GPIO_11 - AF (Alternate Font)
* GPIO_12 - UL (Underline)
* GPIO_13 - SP (Video Suppress)
* GPIO_14 - RV (Reverse Video)
* GPIO_15 - HB (Half Bright)
* GPIO_16 - HBS (Half Bit Shift)
* GPIO_17 - CCLK (Character Clock)
* GPIO_18 - CPIX (Pixel Clock)

* GPIO_20 - Video DAC0
* GPIO_21 - Video DAC1
* GPIO_22 - Video DAC2
