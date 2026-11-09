# arm-intc.library

Exec interrupt servers on the ARM interrupt controller of the Raspberry Pi 3 and Zero 2 (BCM2837), for the AmigaOS drivers of the
PiStorm + Emu68 ecosystem. It does on these boards what `gic400.library` does on the Raspberry Pi 4: a driver adds a
`struct Interrupt` to the line of its peripheral, and the library calls it when the line rings.

```c
struct Library *ArmIntcBase;

if ((ArmIntcBase = OpenLibrary(ARMINTCNAME, 0)) != NULL)
{
    LONG irq = AIC_GetInterrupt(node, 0);          /* the line, from the device tree node of the peripheral */

    if (irq >= 0 && AIC_AddIntServer(irq, &server) == AICERR_OK)
    {
        /* ... the server is called at level 6 ... */
        AIC_RemIntServer(irq, &server);
    }
    CloseLibrary(ArmIntcBase);
}
```

What it does:

- One server on the `INTB_EXTER` chain of exec, present only while a line has a server. It reads the pending registers of the
  ARM control block and calls the servers of the lines that rang.
- Lines named as in the device tree: `AIC_GetInterrupt()` reads them from a node, so a driver does not write numbers in its code.
- Several servers per line, by priority. The first server enables the line and the last one out disables it.
- A storm guard: a line that rings 1000 times in a row without a server claiming it is disabled. A faulty server loses its own
  line, and the machine keeps running.
- A reboot of AmigaOS does not restart the Pi. Before a reset, a reset guard disables the lines of the library, so that none
  rings in the next session before its driver is back.
- It lives side by side with a driver that drives the controller itself: the lines it did not enable are left alone, and the
  interrupts that are not its own go on down the `INTB_EXTER` chain.
- Statistics for each line and for the controller: `AIC_QueryInfoTagList()`.

The API is in [Autodocs/arm-intc.md](Autodocs/arm-intc.md). The same text is in [Autodocs/arm-intc.doc](Autodocs/arm-intc.doc), in
the NDK layout.

**Status:** 0.3, in development. The interface may still change. Tested on a Raspberry Pi 3B under Emu68 1.0.7:
- `aic-test`: every check passed, alone and next to another driver's servers.
- From the timer match to the server: about 10 µs on average.
- The HDMI vertical blank at 60 Hz.
- The reset guard on a real `ColdReboot()`.
- Clients: `brcm-timer.device`, and `dwc2usb.device` for the USB line (network and storage running through it).

## Requirements

- A Raspberry Pi 3B, 3B+, 3A+, Zero 2 W or CM3 under Emu68, with `devicetree.resource` in its ROM.
- The library opens only where the device tree shows the ARM control block (`brcm,bcm2836-armctrl-ic`) as the root interrupt
  parent. On a Raspberry Pi 4, `OpenLibrary()` returns NULL: a driver uses `gic400.library` there.

## Building

The [bebbo m68k-amigaos-gcc](https://github.com/bebbo/amiga-gcc) toolchain with `sfdc`, and CMake 3.30 or later.

```sh
git clone --recursive https://github.com/flype44/arm-intc.library.git
cd arm-intc.library
CC=m68k-amigaos-gcc cmake -S . -B build
make -C build install
```

`build/install/` then holds:
- `libs/arm-intc.library`, for `LIBS:`;
- `sdk/`, for developers: `include/`, `sfd/`, `FD/`, `Autodocs/`;
- `tools/`: `aic-probe`, `aic-test` and the AmigaDOS scripts `aic-run` and `aic-reboot`, which test the library on the machine
  without installing it. `aic-test SHARED` tests the copy that another driver already holds in memory.

The library has a single `.text` section and no writable data, so it can go into the Emu68 ROM. The build fails if that stops
being true.

## Credits

- The two submodules come from the Emu68 ecosystem:
  - [devicetree.resource](https://github.com/michalsc/devicetree.resource), by Michal Schulz: its interface only.
  - [emu68-common](https://github.com/rondoval/emu68-common), by Rondoval: the reset guard is linked into the library.
- The registers of the controllers come from the Linux drivers `irq-bcm2835.c` and `irq-bcm2836.c`. No code was taken from them.

## License

[Mozilla Public License 2.0](LICENSE). emu68-common is under MPL-2.0 OR GPL-2.0+, and is used here under MPL-2.0.
