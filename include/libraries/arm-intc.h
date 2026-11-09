/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifndef LIBRARIES_ARM_INTC_H
#define LIBRARIES_ARM_INTC_H
/*
**	$VER: arm-intc.h 0.3
**
**	Public definitions of "arm-intc.library", the interrupt controller of the ARM side of the BCM2837 family
**	(Raspberry Pi 3B, 3B+, 3A+, Zero 2 W, CM3) for PiStorm / Emu68: exec interrupt servers on the lines of the
**	peripherals. See Autodocs/arm-intc.doc.
**
*/

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif

#define ARMINTCNAME "arm-intc.library"

/*
    The number of a line: bank * 32 + line, where <bank line> is an entry of the "interrupts" property of a node of
    the device tree. AIC_GetInterrupt() reads it from the node: a driver does not write the number in its code.
*/
#define AIC_BANK_ARM            0               /* the lines of the ARM side, 0 to 7 */
#define AIC_BANK_GPU1           1               /* the GPU lines 0 to 31 */
#define AIC_BANK_GPU2           2               /* the GPU lines 32 to 63 */

#define AIC_IRQ(bank, line)     (((ULONG)(bank) << 5) | (ULONG)(line))
#define AIC_IRQ_GPU(n)          (32UL + (ULONG)(n))     /* GPU line n of the datasheet, 0 to 63 */
#define AIC_IRQ_COUNT           96              /* the numbers are below this one; 8 to 31 do not exist */

/* The lines of bank 0, fixed by the silicon */
#define AIC_IRQ_ARM_TIMER       0
#define AIC_IRQ_ARM_MAILBOX     1               /* the mailbox of the VideoCore (mailbox@7e00b880) */
#define AIC_IRQ_ARM_DOORBELL0   2
#define AIC_IRQ_ARM_DOORBELL1   3
#define AIC_IRQ_ARM_GPU0_HALTED 4
#define AIC_IRQ_ARM_GPU1_HALTED 5
#define AIC_IRQ_ARM_ILLEGAL1    6
#define AIC_IRQ_ARM_ILLEGAL0    7

/* The irq of AIC_QueryInfoTagList() for the questions about the controller */
#define AIC_CONTROLLER          0xffffffffUL

/* The base of the tags of this library (TAG_USER + a number picked for it, not registered) */
#define AIC_Dummy               (TAG_USER + 0x41494300)

/* AIC_QueryInfoTagList(AIC_CONTROLLER, ...): ti_Data points to the ULONG that receives the answer */
#define AICI_Version            (AIC_Dummy + 1)     /* (version << 16) | revision of the library */
#define AICI_ControllerAddress  (AIC_Dummy + 2)     /* where the 68k sees the registers of the ARM control block */
#define AICI_LocalAddress       (AIC_Dummy + 3)     /* where the 68k sees the local controller of the cores */
#define AICI_GpuRouting         (AIC_Dummy + 4)     /* the GPU routing register of the local controller (bits 0-1: the core) */
#define AICI_StaleLines         (AIC_Dummy + 5)     /* lines found enabled when the library started, left as they were */
#define AICI_ServerCount        (AIC_Dummy + 6)     /* the servers of every line */
#define AICI_Passes             (AIC_Dummy + 7)     /* level 6 interrupts in which a line of this library rang */
#define AICI_ForeignPasses      (AIC_Dummy + 8)     /* level 6 interrupts in which none did (someone else's) */
#define AICI_ResetGuard         (AIC_Dummy + 9)     /* TRUE when the reset guard is installed */

/* AIC_QueryInfoTagList(irq, ...), about one line */
#define AICI_Enabled            (AIC_Dummy + 32)    /* TRUE when the line is enabled */
#define AICI_Pending            (AIC_Dummy + 33)    /* TRUE when the controller shows the line pending */
#define AICI_Servers            (AIC_Dummy + 34)    /* the servers of the line */
#define AICI_Count              (AIC_Dummy + 35)    /* the times the line rang */
#define AICI_Unclaimed          (AIC_Dummy + 36)    /* the times it rang and no server claimed it */
#define AICI_Stormed            (AIC_Dummy + 37)    /* TRUE when the storm guard disabled it */

/* Errors: the result of the calls that return a LONG, always negative */
#define AICERR_OK               0
#define AICERR_BADIRQ           (-1)            /* not the number of a line */
#define AICERR_BADARGS          (-2)            /* a NULL pointer */
#define AICERR_NOTFOUND         (-3)            /* this interrupt is not a server of this line */
#define AICERR_NOSERVER         (-4)            /* the line has no server: it cannot be enabled */
#define AICERR_NOTOURS          (-5)            /* the node's interrupt parent is not this controller */
#define AICERR_NODT             (-6)            /* no such entry in the node's "interrupts" property */

#endif /* LIBRARIES_ARM_INTC_H */
