Panasonic CX-DP60 Emulator
==========================

The project emulates a Panasonic CX-DP60 CD Changer (CDC), allowing early 1990s car radios to use the CD inputs as an auxilary. Tested with a Panasonic CQ-LR2450A (from an MG RV8) but should work with others from the same era.

![Finished unit](https://raw.githubusercontent.com/cwoffenden/panacdc/master/doc/radio.jpeg)

Without a CDC connected the radio ignores changing to the CD input, hence the need to emulate its presence. This isn't a new idea; others have created emulators for newer CDCs (see [CDCEmu](http://q1.se/cdcemu/), [VAG CDC Faker](http://dev.shyd.de/2013/09/avr-raspberry-pi-vw-beta-vag-cdc-faker/), etc.) but these don't work with older radios.

I started by looking at the DP60's schematics (actually, I started with CDCEmu, which didn't work, so I rolled up my sleves and did this) and recording the traffic with a [Saleae Logic](https://www.saleae.com/). The radio's remote commands look to be using a variant of the [NEC protocol](https://www.vishay.com/docs/80071/dataform.pdf) whereas the CDC's data is a byte orientated synchronous serial stream:
![NEC-like remote data](https://raw.githubusercontent.com/cwoffenden/panacdc/master/doc/logic-1.png)
(Close-up [here](https://raw.githubusercontent.com/cwoffenden/panacdc/master/doc/logic-2.png), with Logic files [here](https://github.com/cwoffenden/panacdc/raw/master/doc/cd-radio-off.logicdata))

It's not necessary to understand or fully decode the communication, just to recognise and replay. The documented source and schematic are available above, but for anyone wishing to simply build it, it's enough to order a PCB (either from the [Eagle schematics](https://github.com/cwoffenden/panacdc/tree/master/pcb), the resulting [Gerbers](https://github.com/cwoffenden/panacdc/raw/master/out/board.zip), or direct from [OSH Park](https://oshpark.com/shared_projects/Xu28lOTu)) and program the flash (via the ISP connector on the board using the [prebuilt firmware](https://github.com/cwoffenden/panacdc/blob/master/out/flash.elf)). It was designed to be built with nothing more than a regular soldering iron.

Parts can be ordered from [Farnell](http://www.farnell.com/), [Mouser](http://www.mouser.com/) or any other componment supplier (the BOM is available [here](https://github.com/cwoffenden/panacdc/blob/master/out/parts.csv)). The finished project has a USB connector to power a Bluetooth or DLNA receiver (tested with an [Arcam miniBlink](http://www.arcam.co.uk/mini.htm)). A [conformal coating](https://raw.githubusercontent.com/cwoffenden/panacdc/master/doc/board.jpeg) was added to protect the board from any moisture.
