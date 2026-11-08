/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/devicetree.h>

#include "base.h"
#include "hw.h"

#define ARMCTRL_COMPATIBLE  "brcm,bcm2836-armctrl-ic"
#define LOCAL_COMPATIBLE    "brcm,bcm2836-l1-intc"

/* The first cell of a property such as #address-cells, or `fallback` when the node does not have it */
ULONG AIC_GetCell(
    APTR DeviceTreeBase,
    APTR key,
    CONST_STRPTR name,
    ULONG fallback)
{
    APTR prop = key ? DT_FindProperty(key, name) : NULL;

    return (prop != NULL && DT_GetPropLen(prop) >= 4) ? *(const ULONG *)DT_GetPropValue(prop) : fallback;
}

static BOOL StrEqual(
    const char *a,
    const char *b)
{
    while (*a != 0 && *a == *b)
    {
        a++;
        b++;
    }

    return *a == *b;
}

/* TRUE when one of the strings of the node's "compatible" (a list of strings, each one ended by a 0) is `name` */
static BOOL IsCompatible(
    APTR DeviceTreeBase,
    APTR key,
    const char *name)
{
    APTR prop = DT_FindProperty(key, (CONST_STRPTR)"compatible");
    const char *s;
    const char *end;

    if (prop == NULL)
        return FALSE;

    s = DT_GetPropValue(prop);
    end = s + DT_GetPropLen(prop);

    while (s < end)
    {
        if (StrEqual(s, name))
            return TRUE;

        while (s < end && *s != 0)
            s++;
        s++;
    }

    return FALSE;
}

/*
    The node whose "phandle" (or the older "linux,phandle") is `phandle`, NULL if none. The tree is walked in depth first order with
    no recursion: the stack of a task that opens the library may be small.
*/
static APTR FindPhandle(
    APTR DeviceTreeBase,
    ULONG phandle)
{
    APTR root = DT_OpenKey((CONST_STRPTR)"/");
    APTR key = DT_GetChild(root, NULL);

    while (key != NULL)
    {
        APTR next;

        if (AIC_GetCell(DeviceTreeBase, key, (CONST_STRPTR)"phandle", 0) == phandle ||
            AIC_GetCell(DeviceTreeBase, key, (CONST_STRPTR)"linux,phandle", 0) == phandle)
            break;

        /* Down to the first child, else to the next sibling, else up until a parent has a next sibling */
        next = DT_GetChild(key, NULL);
        while (next == NULL && key != root)
        {
            APTR parent = DT_GetParent(key);

            next = DT_GetChild(parent, key);
            key = parent;
        }
        key = (key == root && next == NULL) ? NULL : next;
    }

    DT_CloseKey(root);

    return key;
}

/* The phandle of the interrupt parent of a node: its nearest "interrupt-parent", at the node or above it. 0 if there is none. */
ULONG AIC_InterruptParent(
    APTR DeviceTreeBase,
    APTR key)
{
    while (key != NULL)
    {
        APTR prop = DT_FindProperty(key, (CONST_STRPTR)"interrupt-parent");

        if (prop != NULL)
            return DT_GetPropLen(prop) >= 4 ? *(const ULONG *)DT_GetPropValue(prop) : 0;

        key = DT_GetParent(key);
    }

    return 0;
}

/*
    Where the 68k sees the first "reg" of a node: its address on the bus of the parent, translated through the "ranges" of the
    parent. Emu68 has written the address of its own mapping of each range of /soc there (src/raspi/start_rpi64.c,
    map_peripheral_ranges()). Only the low 32 bits of an address are read: the 68k sees no more. 0 if the node has no "reg" or no
    range holds it.
*/
static ULONG RegAddress(
    APTR DeviceTreeBase,
    APTR key)
{
    APTR bus = DT_GetParent(key);
    APTR above = bus ? DT_GetParent(bus) : NULL;
    ULONG child_cells = AIC_GetCell(DeviceTreeBase, bus, (CONST_STRPTR)"#address-cells", 2);
    ULONG size_cells = AIC_GetCell(DeviceTreeBase, bus, (CONST_STRPTR)"#size-cells", 1);
    ULONG parent_cells = AIC_GetCell(DeviceTreeBase, above, (CONST_STRPTR)"#address-cells", 2);
    ULONG entry = child_cells + parent_cells + size_cells;
    APTR reg = DT_FindProperty(key, (CONST_STRPTR)"reg");
    APTR ranges = bus ? DT_FindProperty(bus, (CONST_STRPTR)"ranges") : NULL;
    ULONG address;

    if (reg == NULL || bus == NULL || child_cells == 0 || DT_GetPropLen(reg) < 4 * child_cells)
        return 0;

    address = ((const ULONG *)DT_GetPropValue(reg))[child_cells - 1];

    /* No "ranges": the bus is not mapped to its parent. An empty one: the addresses are the same on both sides. */
    if (ranges == NULL)
        return 0;

    if (DT_GetPropLen(ranges) != 0 && parent_cells != 0 && size_cells != 0)
    {
        const ULONG *r = DT_GetPropValue(ranges);
        ULONG count = DT_GetPropLen(ranges) / (4 * entry);

        for (ULONG i = 0; i < count; i++, r += entry)
        {
            ULONG child = r[child_cells - 1];
            ULONG parent = r[child_cells + parent_cells - 1];
            ULONG size = r[entry - 1];

            /* a parent address above 4 GB is out of the reach of the 68k */
            if (parent_cells > 1 && r[child_cells] != 0)
                continue;

            if (address >= child && address - child < size)
                return address - child + parent;
        }

        return 0;
    }

    return address;
}

/*
    Finds the two controllers, in a task, when the library starts. The root's interrupt parent must be the ARM control block
    (brcm,bcm2836-armctrl-ic, two cells an interrupt), and its own interrupt parent the local controller (brcm,bcm2836-l1-intc),
    which must send the GPU IRQ to core 0: Emu68 makes an interrupt of that core a level 6 interrupt of the 68k. On another SoC
    (the BCM2711 has a GIC) nothing is read from the hardware. Nothing is written.
*/
BOOL AIC_FindController(
    struct ArmIntcBase *base)
{
    APTR DeviceTreeBase = base->aib_DeviceTreeBase;
    APTR root = DT_OpenKey((CONST_STRPTR)"/");
    ULONG phandle = AIC_InterruptParent(DeviceTreeBase, root);
    APTR armctrl = phandle ? FindPhandle(DeviceTreeBase, phandle) : NULL;
    APTR local;

    DT_CloseKey(root);

    if (armctrl == NULL || !IsCompatible(DeviceTreeBase, armctrl, ARMCTRL_COMPATIBLE))
    {
        bug("[arm-intc] the interrupt parent of / is not " ARMCTRL_COMPATIBLE "\n", 0);
        return FALSE;
    }

    if (AIC_GetCell(DeviceTreeBase, armctrl, (CONST_STRPTR)"#interrupt-cells", 0) != 2)
        return FALSE;

    base->aib_Phandle = phandle;
    base->aib_Controller = RegAddress(DeviceTreeBase, armctrl);

    /* The interrupt parent of the ARM control block itself, not inherited: its own property */
    phandle = AIC_GetCell(DeviceTreeBase, armctrl, (CONST_STRPTR)"interrupt-parent", 0);
    local = phandle ? FindPhandle(DeviceTreeBase, phandle) : NULL;

    if (local == NULL || !IsCompatible(DeviceTreeBase, local, LOCAL_COMPATIBLE))
    {
        bug("[arm-intc] the interrupt parent of the ARM control block is not " LOCAL_COMPATIBLE "\n", 0);
        return FALSE;
    }

    base->aib_Local = RegAddress(DeviceTreeBase, local);

    bug("[arm-intc] ARM control block %08lx, local controller %08lx\n", base->aib_Controller, base->aib_Local);

    if (base->aib_Controller == 0 || base->aib_Local == 0)
        return FALSE;

    base->aib_GpuRouting = aic_rd(base->aib_Local, LOCAL_GPU_ROUTING);

    if ((base->aib_GpuRouting & LOCAL_GPU_IRQ_CORE) != 0)
    {
        bug("[arm-intc] the GPU IRQ goes to core %ld, not 0\n", base->aib_GpuRouting & LOCAL_GPU_IRQ_CORE);
        return FALSE;
    }

    return TRUE;
}

/* The registers of each bank, and the lines a bank has (bank 0 has 8) */
static const UBYTE bank_enable[AIC_BANKS] = { ARMCTRL_ENABLE0, ARMCTRL_ENABLE1, ARMCTRL_ENABLE2 };
static const UBYTE bank_disable[AIC_BANKS] = { ARMCTRL_DISABLE0, ARMCTRL_DISABLE1, ARMCTRL_DISABLE2 };
static const ULONG bank_lines[AIC_BANKS] = { 0x000000ffUL, 0xffffffffUL, 0xffffffffUL };

static ULONG CountBits(
    ULONG value)
{
    ULONG count = 0;

    while (value != 0)
    {
        value &= value - 1;
        count++;
    }

    return count;
}

/*
    At the start: the lines still enabled come from a session before a warm reboot of AmigaOS (the Pi did not restart and Emu68
    enables none of them, src/aarch64/intc.c). Their servers are gone: each one is disabled before it can ring with nobody to clear
    it, as Linux does ("Bootloader left irq enabled"). Returns how many there were.
*/
ULONG AIC_CleanSlate(
    struct ArmIntcBase *base)
{
    ULONG count = 0;

    for (ULONG bank = 0; bank < AIC_BANKS; bank++)
    {
        ULONG stale = aic_rd(base->aib_Controller, bank_enable[bank]) & bank_lines[bank];

        if (stale != 0)
        {
            aic_wr(base->aib_Controller, bank_disable[bank], stale);
            count += CountBits(stale);
        }
    }

    return count;
}

/*
    A line on, a line off. The copy of the enables in the base changes with one instruction (or.l / and.l to memory): no interrupt
    comes in the middle, so these may be called from an interrupt. On: the copy first, then the controller, so that the dispatcher
    serves the line from its first ring. Off: the copy first too, so that the dispatcher stops serving it at once.
*/
void AIC_LineOn(
    struct ArmIntcBase *base,
    ULONG irq)
{
    ULONG bank = irq >> 5;
    ULONG bit = 1UL << (irq & 31);

    __asm__ volatile ("or.l %1,%0" : "+m"(base->aib_Enabled[bank]) : "d"(bit) : "cc");
    aic_wr(base->aib_Controller, bank_enable[bank], bit);
}

void AIC_LineOff(
    struct ArmIntcBase *base,
    ULONG irq)
{
    ULONG bank = irq >> 5;
    ULONG bit = 1UL << (irq & 31);

    __asm__ volatile ("and.l %1,%0" : "+m"(base->aib_Enabled[bank]) : "d"(~bit) : "cc");
    aic_wr(base->aib_Controller, bank_disable[bank], bit);
}
