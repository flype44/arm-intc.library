/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <stddef.h>

#include <exec/types.h>
#include <exec/interrupts.h>

#include "base.h"
#include "hw.h"

/*
    The two places where the Z flag matters are written in assembly: a C epilogue may set it again from a restored register (a
    MOVE that restores one register sets Z, a MOVEM does not), and which one GCC emits changes with an innocent edit.

    AIC_Dispatcher is the INTB_EXTER server that Exec calls, with A1 = is_Data (the base). It calls AIC_Dispatch() and sets Z from
    its result with a TST after the call: Z clear (handled, the chain ends) when one of our lines rang, Z set (someone else's level
    6 interrupt, the chain goes on) when none did. Exec lets a server change D0, D1, A0, A1, A5 and A6; AIC_Dispatch() is a C
    function and keeps every register a C function keeps.

    AIC_CallServer(interrupt, irq, SysBase) calls one server of a line with the registers of an Exec interrupt server: A1 = is_Data,
    A5 = is_Code, A6 = SysBase, D0 = the number of the line, and reads the Z flag it returns: 1 when it claimed the interrupt (Z
    clear), 0 when it did not. A5 and A6 are kept for the C caller; D0, D1, A0 and A1 are scratch for C as for the server.
*/
/* gcc 6.5 takes no operand in a file-scope asm: the offsets in struct Interrupt are written out, and checked here */
typedef char is_data_at_14[(offsetof(struct Interrupt, is_Data) == 14) ? 1 : -1];
typedef char is_code_at_18[(offsetof(struct Interrupt, is_Code) == 18) ? 1 : -1];

__asm__(
    "	.text\n"
    "	.even\n"
    "	.globl	_AIC_Dispatcher\n"
    "_AIC_Dispatcher:\n"
    "	move.l	a1,-(sp)\n"                    /* the base, the argument of AIC_Dispatch() */
    "	jsr	_AIC_Dispatch\n"
    "	addq.l	#4,sp\n"
    "	tst.l	d0\n"                          /* Z from the result, nothing after it touches the flags */
    "	rts\n"
    "\n"
    "	.even\n"
    "	.globl	_AIC_CallServer\n"
    "_AIC_CallServer:\n"
    "	movem.l	a5-a6,-(sp)\n"                 /* 8 bytes: the arguments are at 12, 16 and 20 */
    "	movea.l	12(sp),a0\n"                   /* the struct Interrupt */
    "	move.l	16(sp),d0\n"                   /* D0 = the number of the line */
    "	movea.l	20(sp),a6\n"                   /* A6 = SysBase */
    "	movea.l	14(a0),a1\n"                   /* A1 = is_Data */
    "	movea.l	18(a0),a5\n"                   /* A5 = is_Code */
    "	jsr	(a5)\n"
    "	sne	d0\n"                          /* the flag the server left: Z clear = claimed */
    "	movem.l	(sp)+,a5-a6\n"
    "	andi.l	#1,d0\n"
    "	rts\n");

/* The bits of the pending register of bank 0 that say a line of bank 1 or bank 2 rings: the bit "something in pending 1 / 2",
   and the shortcuts, which show some GPU lines in bank 0 directly (Linux: SHORTCUT1_MASK, SHORTCUT2_MASK). Whether bits 8 and 9
   are also set for a line that has a shortcut is not said: either of them makes the dispatcher read the bank itself. */
#define PENDING0_LINES          0x000000ffUL
#define PENDING0_BANK1          ((1UL << 8) | 0x00007c00UL)
#define PENDING0_BANK2          ((1UL << 9) | 0x001f8000UL)

#define AIC_MAX_PASSES          8           /* readings of the pending registers in one level 6 interrupt */

static const UBYTE bank_pending[AIC_BANKS] = { ARMCTRL_PENDING0, ARMCTRL_PENDING1, ARMCTRL_PENDING2 };

/* One line rang: its servers in order until one claims it; the storm guard when none does */
static void ServeLine(
    struct ArmIntcBase *base,
    ULONG irq)
{
    struct AicLine *line = &base->aib_Lines[irq];
    struct Interrupt *server = line->al_Head;
    ULONG claimed = 0;

    line->al_Count++;

    while (server != NULL && !claimed)
    {
        claimed = AIC_CallServer(server, irq, base->aib_SysBase);
        server = (struct Interrupt *)server->is_Node.ln_Succ;
    }

    if (claimed)
    {
        line->al_Run = 0;
        return;
    }

    line->al_Unclaimed++;

    /* A line with no server should not be enabled: switched off at once. One that nobody claims, AIC_STORM_LIMIT times in a row,
       would take the machine with it: switched off too, and said in AICI_Stormed. AIC_EnableInt() gives it back. */
    if (line->al_Head == NULL || ++line->al_Run >= AIC_STORM_LIMIT)
    {
        AIC_LineOff(base, irq);
        line->al_Stormed = TRUE;
        line->al_Run = 0;
    }
}

/*
    The body of the INTB_EXTER server, at level 6. Reads which of our lines ring, serves them from the lowest number up, then reads
    again: a line that rings while the others are served is served in the same interrupt. A line still high after AIC_MAX_PASSES
    readings raises the interrupt again as soon as Exec leaves level 6: nothing is lost. Returns non zero when one of our lines
    rang.
*/
ULONG AIC_Dispatch(
    struct ArmIntcBase *base)
{
    ULONG controller = base->aib_Controller;
    ULONG passes = 0;

    while (passes < AIC_MAX_PASSES)
    {
        ULONG pending0 = aic_rd(controller, ARMCTRL_PENDING0);
        ULONG ring[AIC_BANKS];
        ULONG bank;

        /* Only the lines this library enabled: a line the pending registers show for another reason is not ours to serve */
        ring[0] = pending0 & PENDING0_LINES & base->aib_Enabled[0];
        ring[1] = (pending0 & PENDING0_BANK1) && base->aib_Enabled[1] ?
            aic_rd(controller, bank_pending[1]) & base->aib_Enabled[1] : 0;
        ring[2] = (pending0 & PENDING0_BANK2) && base->aib_Enabled[2] ?
            aic_rd(controller, bank_pending[2]) & base->aib_Enabled[2] : 0;

        if ((ring[0] | ring[1] | ring[2]) == 0)
            break;

        passes++;

        for (bank = 0; bank < AIC_BANKS; bank++)
        {
            ULONG bits = ring[bank];
            ULONG irq = bank << 5;

            for (; bits != 0; bits >>= 1, irq++)
            {
                if (bits & 1)
                    ServeLine(base, irq);
            }
        }

        /* A full barrier under Emu68: the writes of the servers that clear their devices land before the pending registers are
           read again, which would show the line still high otherwise */
        __asm__ volatile ("nop" : : : "memory");
    }

    if (passes != 0)
        base->aib_Passes++;
    else
        base->aib_ForeignPasses++;

    return passes != 0;
}
