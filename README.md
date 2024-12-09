# z80pack on Waveshare RP2040-GEEK/RP2350-GEEK

While looking around for RP2040 based systems that can easily be
used by those who don't want to tinker with the hardware, I found this:

[Waveshare RP2040-GEEK product page](https://www.waveshare.com/rp2040-geek.htm) and
[Waveshare RP2040-GEEK Wiki](https://www.waveshare.com/wiki/RP2040-GEEK)

and recently this:

[Waveshare RP2350-GEEK product page](https://www.waveshare.com/rp2350-geek.htm) and
[Waveshare RP2350-GEEK Wiki](https://www.waveshare.com/wiki/RP2350-GEEK)

To build z80pack for this device you need to have the SDK for RP2040/RP2350
based devices installed and configured. The SDK manual has detailed
instructions how to install on all major PC platforms, it is available here:
[Raspberry Pi Pico Documentation](https://www.raspberrypi.com/documentation/microcontrollers/raspberry-pi-pico.html)

Then clone the GitHub repositories:

1. clone z80pack: git clone https://github.com/udo-munk/z80pack.git
2. checkout dev branch: cd z80pack; git checkout dev; cd ..
3. clone this: git clone https://github.com/udo-munk/RP2xxx-GEEK-80.git

To build the application for the Waveshare RP2040-GEEK:
```
cd RP2xxx-GEEK-80/srcsim
mkdir build
cd build
cmake -G "Unix Makefiles" ..
make -j
```

For the Waveshare RP2350-GEEK use (you can also specify rp2350-riscv
as the platform if you have the appropriate RISC-V toolchain installed):
```
cd RP2xxx-GEEK-80/srcsim
mkdir build
cd build
cmake -D PICO_PLATFORM=rp2350-arm-s -G "Unix Makefiles" ..
make -j
```

If you don't want to build it yourself, the directories flash-rp2040,
flash-rp2350-arm-s, and flash-rp2350-riscv contain the
current build. Flash `picosim.uf2` into the device, and then prepare a
MicroSD card.

In the root directory of the card create these directories:
```
CONF80
CODE80
DISKS80
```

Into the CODE80 directory copy all the .bin files from src-examples.
Into the DISKS80 directory copy the disk images from disks.
CONF80 is used to save the configuration, nothing more to do there,
the directory must exist though.

I also attached a battery backed RTC to the I2C port, so that I don't
have to update date/time information my self anymore. This is optional,
the firmware will check if such a device is available, and if found use
time/date informations from it.

![image](https://github.com/udo-munk/RP2xxx-GEEK-80/blob/main/resources/RTC.png "battery backed RTC")

In the latest build the serial UART is enabled, so that one can connect
a terminal. I tested this with connecting a Pico probe to the UART.

![image](https://github.com/udo-munk/RP2xxx-GEEK-80/blob/main/resources/terminal.jpg "Pico probe terminal")
