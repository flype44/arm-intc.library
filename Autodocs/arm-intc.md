# arm-intc.library

## Contents

- [Background](#background)
- [`AIC_AddIntServer`](#aic_addintserver) - add an interrupt server to a line
- [`AIC_DisableInt`](#aic_disableint) - disable a line and keep its servers
- [`AIC_EnableInt`](#aic_enableint) - enable a line again
- [`AIC_GetInterrupt`](#aic_getinterrupt) - read the number of a line from the device tree
- [`AIC_QueryInfoTagList`](#aic_queryinfotaglist) - ask about the controller or a line
- [`AIC_RemIntServer`](#aic_remintserver) - take an interrupt server off a line

## Background

### Purpose

arm-intc.library puts exec interrupt servers on the lines of the peripherals of the BCM2837 family (Raspberry Pi 3B, 3B+, 3A+, Zero 2 W, CM3), for PiStorm / Emu68. It is the counterpart of gic400.library, which does the same on the BCM2711 (Pi 4, Pi 400, CM4).

A line of a peripheral goes through the ARM control block of the SoC (brcm,bcm2836-armctrl-ic), then through the local controller of the cores (brcm,bcm2836-l1-intc) to the first core. Emu68 turns an interrupt of that core into a level 6 interrupt of the 68k (INTB_EXTER). The library has one server on INTB_EXTER: it finds which lines rang and calls their servers.

### Boards

The library opens only where the interrupt parent of the root of the device tree is the ARM control block. Elsewhere OpenLibrary() returns NULL: on a BCM2711 a driver uses gic400.library.

The library never writes the FIQ register of the ARM control block, nor the local controller: they belong to Emu68.

### The numbers

A line is named by bank * 32 + line, where <bank line> is an entry of the "interrupts" property of a node of the device tree: 0 to 7 are the lines of the ARM side (bank 0), 32 to 63 the GPU lines 0 to 31 (bank 1), 64 to 95 the GPU lines 32 to 63 (bank 2). AIC_GetInterrupt() reads the number from the node. <libraries/arm-intc.h> names the lines of bank 0, which the silicon fixes; AIC_IRQ() and AIC_IRQ_GPU() build the others.

### Servers

A server is a struct Interrupt, called at level 6 with the registers of an exec interrupt server: A1 = is_Data, A5 = is_Code, A6 = SysBase, and D0 = the number of the line. It may change D0, D1, A0, A1, A5 and A6. It returns non zero, with the Z flag clear, when the interrupt was its own; zero, with Z set, when it was not.

The lines are level lines: a line stays high until its device is cleared. A server reads the status of its device, clears it, then returns. A line that is still high rings again at once.

Several servers may share a line. They are called in the order of ln_Pri (highest first, then in the order they were added) until one claims the interrupt. The first server of a line enables it, the last one out disables it.

A server that the compiler writes may lose the Z flag in its epilogue (a MOVE that restores a register sets it again). Check the code of a C server, or write it in assembly.

### Storm guard

A line that rings 1000 times in a row and that no server claims is disabled by the library, and AICI_Stormed tells it. A wrong server costs its own line, not the machine. AIC_EnableInt() enables the line again.

### Warm reboot

When AmigaOS restarts and the Raspberry Pi does not, the lines that were enabled stay enabled, with no server to clear their devices.

The first server added installs a reset guard (reset_guard of emu68-common, by Rondoval). On ColdReboot() and on Ctrl-Amiga-Amiga it disables every line that the library enabled, once, before the reset goes on. The guard patches exec.library/ColdReboot() with SetFunction() and adds a reset handler to keyboard.device. AICI_ResetGuard tells whether it is installed. It stays until the library is expunged.

When it starts, the library counts the lines that it finds enabled (AICI_StaleLines) and leaves them as they are. Such a line may belong to a driver that drives the controller itself (see OTHER DRIVERS).

A driver clears its device before it adds its server.

### Other drivers

A driver may enable a line of the ARM control block by itself and put its own server on INTB_EXTER, without this library (for example a driver that falls back to its own server when the library is absent). The two live side by side: the library serves only the lines that it enabled, and when none of them rang it lets the INTB_EXTER chain go on (AICI_ForeignPasses counts these interrupts). The enable and disable registers change only the lines written with a 1, so neither side undoes the other.

A line must not be shared that way. When a server of this library is added to a line that another driver enabled by itself, the last server of the library that is removed disables the line, and the other driver loses it. Such a driver should add its server through this library instead.

### Versions

| Version | Date | Changes |
|---|---|---|
| 0.3 | 09.11.2026 | First version. |

## AIC_AddIntServer

Add an interrupt server to a line

```c
LONG AIC_AddIntServer(ULONG irq, struct Interrupt *interrupt);    /* d0,a1 */
```

| Input | Register | Meaning |
|---|---|---|
| `irq` | d0 | the number of the line (see THE NUMBERS) |
| `interrupt` | a1 | the server: is_Code, is_Data, and ln_Pri for its place on the line |

Adds the server to the line, by ln_Pri. When it is the first server of the line, the line is enabled: the server may be called before the function returns. The interrupt stays the caller's, and must not move or be freed before AIC_RemIntServer().

Adding a server that is already on the line does nothing and returns AICERR_OK.

The first server of the library puts the server of the library on the INTB_EXTER chain of exec, at priority 100; the last one out takes it off.

The first server also installs the reset guard (see WARM REBOOT), which opens keyboard.device and patches ColdReboot(). Where it cannot be installed, the server is added all the same, and the next server tries again.

Call it from a task.

**Result**

- `error`: AICERR_OK, AICERR_BADIRQ, AICERR_BADARGS (interrupt or its is_Code is NULL).

**Example**

```c
    LONG irq = AIC_GetInterrupt(key, 0);

    if (irq >= 0)
    {
        /* clear the device first: the line is enabled at once */
        is.is_Node.ln_Type = NT_INTERRUPT;
        is.is_Node.ln_Pri = 0;
        is.is_Node.ln_Name = "my driver";
        is.is_Data = mydata;
        is.is_Code = (VOID (*)())MyServer;

        if (AIC_AddIntServer(irq, &is) == AICERR_OK)
            ...
    }
```

See also: [`AIC_RemIntServer`](#aic_remintserver), [`AIC_GetInterrupt`](#aic_getinterrupt), `gic400.library/AddIntServerEx`, `exec.library/AddIntServer`

## AIC_DisableInt

Disable a line and keep its servers

```c
LONG AIC_DisableInt(ULONG irq);    /* d0 */
```

| Input | Register | Meaning |
|---|---|---|
| `irq` | d0 | the number of a line that has servers |

Disables the line: its servers are not called until AIC_EnableInt(). The calls do not nest: one AIC_EnableInt() enables what any number of AIC_DisableInt() disabled.

The line is disabled for every server that shares it.

It may be called from an interrupt.

**Result**

- `error`: AICERR_OK, AICERR_BADIRQ, AICERR_NOSERVER.

See also: [`AIC_EnableInt`](#aic_enableint)

## AIC_EnableInt

Enable a line again

```c
LONG AIC_EnableInt(ULONG irq);    /* d0 */
```

| Input | Register | Meaning |
|---|---|---|
| `irq` | d0 | the number of a line that has servers |

Enables a line that AIC_DisableInt() or the storm guard disabled, and clears AICI_Stormed. A line that is enabled stays so.

It may be called from an interrupt.

**Result**

- `error`: AICERR_OK, AICERR_BADIRQ, AICERR_NOSERVER (a line with no server is never enabled).

See also: [`AIC_DisableInt`](#aic_disableint)

## AIC_GetInterrupt

Read the number of a line from the device tree

```c
LONG AIC_GetInterrupt(APTR key, ULONG index);    /* a0,d0 */
```

| Input | Register | Meaning |
|---|---|---|
| `key` | a0 | a key of devicetree.resource, the node of the device |
| `index` | d0 | which entry of its "interrupts" property, from 0 |

Reads the entry of the "interrupts" property of the node and gives the number of its line. The interrupt parent of the node (the nearest "interrupt-parent", up the tree) must be the ARM control block of this library: the entry of a node of another controller is refused, it is not taken for a line of this one.

The "interrupts-extended" property is not read.

**Result**

- `irq`: the number of the line, 0 or more; or AICERR_BADARGS (key is NULL), AICERR_NODT (no such entry), AICERR_NOTOURS (another interrupt parent), AICERR_BADIRQ (an entry that names no line).

**Example**

```c
    APTR key = DT_OpenKey("/soc/mailbox@7e00b880");
    LONG irq = AIC_GetInterrupt(key, 0);  /* AIC_IRQ_ARM_MAILBOX */

    DT_CloseKey(key);
```

See also: [`AIC_AddIntServer`](#aic_addintserver), `devicetree.resource/DT_OpenKey`

## AIC_QueryInfoTagList

Ask about the controller or a line

```c
ULONG AIC_QueryInfoTagList(ULONG irq, CONST struct TagItem *tagList);    /* d0,a0 */
```

```c
ULONG AIC_QueryInfoTags(ULONG irq, ULONG tag1Type, ...);
```

| Input | Register | Meaning |
|---|---|---|
| `irq` | d0 | AIC_CONTROLLER, or the number of a line |
| `tagList` | a0 | the questions: each ti_Data points to the ULONG that receives the answer |

Answers the questions of the tag list, about the controller when irq is AIC_CONTROLLER, about one line otherwise. A question about a line asked for the controller, or the other way round, is skipped.

It may be called from an interrupt.

| Tag | Meaning |
|---|---|
| `AICI_Version` | the version in the upper word, the revision in the lower word (controller) |
| `AICI_ControllerAddress` | where the 68k sees the ARM control block (controller) |
| `AICI_LocalAddress` | where the 68k sees the local controller (controller) |
| `AICI_GpuRouting` | the GPU routing register of the local controller (controller) |
| `AICI_StaleLines` | lines found enabled at the start, left as they were (controller) |
| `AICI_ServerCount` | the servers of every line (controller) |
| `AICI_Passes` | level 6 interrupts in which a line of the library rang (controller) |
| `AICI_ForeignPasses` | level 6 interrupts in which none did (controller) |
| `AICI_ResetGuard` | TRUE when the reset guard is installed (controller) |
| `AICI_Enabled` | TRUE when the line is enabled (line) |
| `AICI_Pending` | TRUE when the controller shows the line pending (line) |
| `AICI_Servers` | the servers of the line (line) |
| `AICI_Count` | the times the line rang (line) |
| `AICI_Unclaimed` | the times it rang and no server claimed it (line) |
| `AICI_Stormed` | TRUE when the storm guard disabled it (line) |

**Result**

- `count`: how many questions were answered; 0 when irq is not a line nor AIC_CONTROLLER.

See also: [`AIC_AddIntServer`](#aic_addintserver)

## AIC_RemIntServer

Take an interrupt server off a line

```c
LONG AIC_RemIntServer(ULONG irq, struct Interrupt *interrupt);    /* d0,a1 */
```

| Input | Register | Meaning |
|---|---|---|
| `irq` | d0 | the number of the line |
| `interrupt` | a1 | a server given to AIC_AddIntServer() for this line |

Takes the server off the line. When it was the last one, the line is disabled. Once the function returns, the server is not called any more.

The driver quiets its device first: a line shared with other servers stays enabled.

Call it from a task.

**Result**

- `error`: AICERR_OK, AICERR_BADIRQ, AICERR_BADARGS, AICERR_NOTFOUND (it was not a server of this line).

See also: [`AIC_AddIntServer`](#aic_addintserver)
