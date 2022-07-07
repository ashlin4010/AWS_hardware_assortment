# AWS Hardware Assortment
This is a assortment of pcbs made for a Convergent Technologies AWS.
These designs may contain faults or errors that could destroy or damage the themselves or the AWS.
USE AT YOUR OWN RISK

## Bill of Materials
At this stage I have not create a bill of materials, if and when I make updates  to the designs I will try to improve the documentation.

## USB To AWS Cluster Interface
The AWS communicates with two differential pairs, one for clock and one for data.
This board uses the
[Adafruit FT232H Breakout](https://www.adafruit.com/product/2264) combined with differential drivers/receivers to communicate with the cluster network.
It includes some leds controllable from the FT232H and overvoltage protection via a zener.
Note: This adaptor unlike the AWS is not able to transmit and receive to its self.

![Alt text](images/AWS_FT232H_RS_422_PCB.png?raw=true "AWS FT232H RS422 PCB")

## CPU Interface Board
This is effectively a breakout board that allows an Turbo CPU board to interface with peripheralsand and receive power
inplace of the original AWS motherboard. It has the following functions: cluster connectors, keyboard connector,
speaker breakout and volume, power LED, reset button and LED, video interface connector, and power input.

![Alt text](images/CPU_Interface_Board_PCB.png?raw=true "CPU Interface Board PCB")

## Composite Adapter (Untested)

The Composite Adapter is an attempts to convert the AWSs ttl video to composite. The adapter has been built to be modular,
one part contains a power supply circuit and differential line receiver.
The other part contains the circuitry that produces the sync signal and is responsible for combining everything into composite.
At this stage this adapter has not been testred and most of the resistor values are guesses hence the potentiometers.
Note: In order to create an NTSC compatible signal the AWS horizontal scan frequency needs to be decreased. This can be done via an external
clock at the J3 connector or Video Debug PCB (Pixel Clock of 14.1606MHz ≈ 15.734kHz horizontal scan).

![Alt text](images/Composite_Adapter_PCB.png?raw=true "Composite Adapter PCB")

## Video Debug Borad
The Video Debug Borad has to functions, provieds an external video clock and LED indicators for the character ROM.
The Video Debug Board is able to provide a small amount of insight into what the CRT controller is doing.
But for the most part is more decorative than functional.

![Alt text](images/Video_Debug_PCB.png?raw=true "Video Debug PCB")