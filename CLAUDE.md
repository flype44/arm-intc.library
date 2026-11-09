# arm-intc.library

AmigaOS disk library for PiStorm / Emu68 on the BCM2837 family (Raspberry Pi 3B, 3B+, 3A+, Zero 2 W, CM3): exec interrupt servers
on the lines of the peripherals, through the ARM control interrupt controller of the SoC. The counterpart of `gic400.library`, which
does the same on the BCM2711. ROM-able, so that it can join the Emu68 ROM.

## Layout

| Path | Content |
|---|---|
| `sfd/arm-intc_lib.sfd` | The interface: 6 vectors, bias 30, base `_ArmIntcBase`, prefix `AIC_` |
| `include/libraries/arm-intc.h` | Line numbers, `AICI_*` tags, `AICERR_*` errors |
| `Autodocs/arm-intc.json` | The source of the Autodoc; `arm-intc.doc` (NDK layout) and `arm-intc.md` are generated from it |
| `src/main.c`, `init.c`, `open.c`, `close.c`, `expunge.c`, `end.c` | RomTag (`RTF_AUTOINIT`) and life cycle; `Init` fails where the controllers are not |
| `src/controller.c` | The two controllers in the device tree: phandles, `compatible`, `reg` through the parent `ranges`, GPU routing; the count of the lines enabled at start (not touched); a line on, a line off; `AIC_PrepareReset()` for the reset guard |
| `src/dispatch.c` | The `INTB_EXTER` server: two assembly stubs (the entry from Exec, the call of one server, where the Z flag matters) and the decoding of the pending registers in C |
| `src/aic_*.c` | One file per function of the SFD |
| `src/hw.h` | Register offsets and the 32-bit access helpers |
| `testcases/aic-probe.c` | What the library relies on, proved on the machine without it (links `controller.c` and `aic_getinterrupt.c`): read only, or `TIMER` for one real line (compare 1 of the system timer, line 33) |
| `testcases/aic-test.c` | The library through its calls (opened from a file, `LIB=`): errors, one line, a chain, disable/enable, the last server out, `STORM`, `VBLANK`, expunge, a line enabled by another driver; `REBOOT`, `CONTROL`, `LINEOFF` for `aic-reboot`; `SHARED` for the copy another driver holds (opened by name, counts relative to its servers, no expunge, its lines checked at the end) |
| `testcases/aic-run` | AmigaDOS script: every test in order, each step only when the one before passed, results in `aic-results.txt` |
| `testcases/aic-reboot` | AmigaDOS script, run three times, on a machine where no driver holds the library: `aic-test REBOOT` (a server on line 33, `ColdReboot()`); `aic-probe` (line 33 disabled by the guard), then `CONTROL` (line 33 by hand, no library, `ColdReboot()`); `aic-probe` (line 33 survived), `LINEOFF` |
| `cmake/` | `sfdc()`, the version string, `romable.cmake` (the build fails if the library has a section other than `.text`) |
| `devicetree.resource/` | Submodule, Michal Schulz's repository: its SFD gives `proto/devicetree.h` (the resource itself is in the ROM of Emu68) |
| `emu68-common/` | Submodule, Rondoval's repository: `src/reset_guard.c` and `src/memset_movem.S` are linked into the library, nothing else |
| `README.md` | The GitHub README: what, status, requirements, build, credits; no API (the Autodoc is the reference) |

## Design

- Emu68 turns an IRQ taken on ARM core 0 into a 68k level 6 interrupt (`INTB_EXTER`), whatever the controller. The library has
  one `INTB_EXTER` server, present only while at least one line has a server: it reads the pending registers of the ARM control
  block (`brcm,bcm2836-armctrl-ic`) and calls the servers of the lines that rang.
- A line is `bank * 32 + line`, the `<bank line>` of the device tree: 0-7 the ARM lines, 32-63 GPU 0-31, 64-95 GPU 32-63.
  `AIC_GetInterrupt()` reads it from a node and refuses a node of another interrupt parent.
- Every line is level triggered, with no priority. Several servers per line, by `ln_Pri`; Z clear ends the walk; the first server
  enables the line, the last one out disables it.
- The Z flag never depends on a compiler epilogue: the entry from Exec ends with `tst.l d0` after the C body, and a server is
  called by a stub that reads its flag with `sne`. gcc 6.5 takes no operand in a file-scope `asm`: the offsets of
  `struct Interrupt` are written out and checked at compile time.
- The dispatcher reads pending basic first; bank 1 or 2 only when bit 8 / 9 or one of their shortcuts is set. Every bank is ANDed
  with the library's own copy of the enables, which changes with one `or.l` / `and.l` to memory (safe from an interrupt). Up to
  8 readings in one level 6 interrupt, a `nop` (a full barrier under Emu68) after each pass. Dispatcher priority 100 on
  `INTB_EXTER`, as gic400.library.
- Lists change under `Disable()`: the dispatcher joins the chain before the first line is enabled and leaves after the last one is
  disabled.
- Storm guard: a line that rings 1000 times in a row with no server claiming it is disabled and flagged.
- At start the lines already enabled are counted, never disabled: they may be another driver's (one that drives the controller
  itself, with its own `INTB_EXTER` server). Only the lines of the library are served; the chain goes on otherwise. A
  line must not be shared that way (the library's last server out would disable it). The FIQ register and the local
  controller (`brcm,bcm2836-l1-intc`) belong to Emu68: read, never written. The GPU interrupt must be routed to core 0.
- Before a reset of AmigaOS, the reset guard of emu68-common (`ColdReboot()` patched with `SetFunction()`, a keyboard.device reset
  handler) calls `AIC_PrepareReset()`: every line of the library disabled, MMIO only (interrupt safe). Installed by the first
  `AIC_AddIntServer()` (a task, not `Init`: in the ROM keyboard.device may not exist yet), removed by the expunge; when
  `ColdReboot()` was patched after it, the removal fails and the library stays. `AICI_ResetGuard` tells it.
- Where the root interrupt parent is not the ARM control block (BCM2711), `OpenLibrary()` fails.

## Conventions

- bebbo gcc 6.5, `-Os -m68040 -msmall-code`, no libgcc, no C library, no writable global data, zero warnings.
- SoC registers through 32-bit helpers only: volatile, little endian swapped, `move.l Dn,Dn` after each read and a `nop` after each
  write (no merging by the JIT; the `nop` is also a barrier under Emu68).
- A function is added at the end of the SFD, never inserted, and the Autodoc changes with it. One file per function. MPL-2.0 header.
- English for code, comments, docs and commit messages.
- The Linux drivers (`irq-bcm2835.c`, `irq-bcm2836.c`) are a source of facts about the hardware, never of code.

## Build

```sh
git submodule update --init
CC=m68k-amigaos-gcc cmake -S . -B build && make -C build install
```

The build must print no warning. `-DDEVICETREE_DIR=<path>` and `-DEMU68_COMMON_DIR=<path>` take the sources of the submodules from elsewhere,
`-DAIC_DEBUG=ON` sends debug messages to `RawPutChar()`.
