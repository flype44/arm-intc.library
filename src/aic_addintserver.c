/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <exec/interrupts.h>
#include <hardware/intbits.h>
#include <proto/exec.h>

#include <common/compiler.h>

#include "base.h"

/*
    Adds a server to a line, by ln_Pri, highest first, after those of the same priority. The dispatcher walks the chains at level 6:
    they change with the interrupts disabled. The first server of the library puts the dispatcher on the INTB_EXTER chain, before
    any line is enabled; the first server of a line enables it, last, so that the server may be called at once.
*/
LONG L_AIC_AddIntServer(
    REGARG(ULONG irq, "d0"),
    REGARG(struct Interrupt *interrupt, "a1"),
    REGARG(struct ArmIntcBase *base, "a6"))
{
    struct ExecBase *SysBase = base->aib_SysBase;
    struct AicLine *line;
    struct Interrupt *prev = NULL;
    struct Interrupt *next;

    if (interrupt == NULL || interrupt->is_Code == NULL)
        return AICERR_BADARGS;

    if (!AIC_ValidIrq(irq))
        return AICERR_BADIRQ;

    line = &base->aib_Lines[irq];

    Disable();

    for (next = line->al_Head; next != NULL; next = (struct Interrupt *)next->is_Node.ln_Succ)
    {
        if (next == interrupt)
        {
            Enable();
            return AICERR_OK;
        }
    }

    next = line->al_Head;
    while (next != NULL && next->is_Node.ln_Pri >= interrupt->is_Node.ln_Pri)
    {
        prev = next;
        next = (struct Interrupt *)next->is_Node.ln_Succ;
    }

    if (base->aib_ServerCount == 0)
        AddIntServer(INTB_EXTER, &base->aib_Dispatcher);

    interrupt->is_Node.ln_Succ = (struct Node *)next;
    interrupt->is_Node.ln_Pred = (struct Node *)prev;
    if (next != NULL)
        next->is_Node.ln_Pred = (struct Node *)interrupt;
    if (prev != NULL)
        prev->is_Node.ln_Succ = (struct Node *)interrupt;
    else
        line->al_Head = interrupt;

    line->al_Servers++;
    base->aib_ServerCount++;

    if (line->al_Servers == 1)
    {
        line->al_Run = 0;
        line->al_Stormed = FALSE;
        AIC_LineOn(base, irq);
    }

    Enable();

    return AICERR_OK;
}
