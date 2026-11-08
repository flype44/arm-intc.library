/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifndef _HW_H
#define _HW_H

#include <exec/types.h>

/*
    The ARM control block (brcm,bcm2836-armctrl-ic, bus 0x7e00b200), offsets from the address of its "reg". Linux:
    drivers/irqchip/irq-bcm2835.c. The enable and disable registers take a 1 for each line to change, a 0 changes nothing.
*/
#define ARMCTRL_PENDING0        0x00        /* bank 0: lines 0-7, bit 8 something in PENDING1, bit 9 in PENDING2, 10-20 shortcuts */
#define ARMCTRL_PENDING1        0x04        /* bank 1: GPU lines 0-31 */
#define ARMCTRL_PENDING2        0x08        /* bank 2: GPU lines 32-63 */
#define ARMCTRL_FIQ_CONTROL     0x0c        /* Emu68's: never written here */
#define ARMCTRL_ENABLE1         0x10
#define ARMCTRL_ENABLE2         0x14
#define ARMCTRL_ENABLE0         0x18        /* "enable basic" */
#define ARMCTRL_DISABLE1        0x1c
#define ARMCTRL_DISABLE2        0x20
#define ARMCTRL_DISABLE0        0x24        /* "disable basic" */

/*
    The local controller of the cores (brcm,bcm2836-l1-intc, bus 0x40000000). Linux: include/linux/irqchip/irq-bcm2836.h.
    Emu68 owns it (src/aarch64/intc.c): only read here.
*/
#define LOCAL_GPU_ROUTING       0x0c        /* bits 0-1: the core the GPU IRQ goes to, bits 2-3: the core of the GPU FIQ */
#define LOCAL_GPU_IRQ_CORE      0x03

#define LE32(x)                 __builtin_bswap32(x)

/*
    A register is read and written with one 32 bit move: the peripherals take no narrower access, and GCC may turn a volatile
    read-modify-write into byte accesses. The JIT of Emu68 merges two moves that follow each other into one access of the ARM:
    another instruction after each access keeps them apart. After a read, move.l Dn,Dn (it changes nothing); after a write, a nop,
    which is also a full barrier under Emu68: the write has reached the device before what follows.
*/
static inline ULONG aic_rd(ULONG block, ULONG reg)
{
    ULONG value = *(volatile ULONG *)(block + reg);

    __asm__ volatile ("move.l %0,%0" : "+d"(value) : : "cc");

    return LE32(value);
}

static inline void aic_wr(ULONG block, ULONG reg, ULONG value)
{
    *(volatile ULONG *)(block + reg) = LE32(value);
    __asm__ volatile ("nop");
}

#endif /* _HW_H */
