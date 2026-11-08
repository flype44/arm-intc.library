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
    Takes a server off a line, with the interrupts disabled: once it returns, the server is not called any more. The last server
    of a line disables it; the last server of the library takes the dispatcher off the INTB_EXTER chain.
*/
LONG L_AIC_RemIntServer(
    REGARG(ULONG irq, "d0"),
    REGARG(struct Interrupt *interrupt, "a1"),
    REGARG(struct ArmIntcBase *base, "a6"))
{
    struct ExecBase *SysBase = base->aib_SysBase;
    struct AicLine *line;
    struct Interrupt *prev = NULL;
    struct Interrupt *server;

    if (interrupt == NULL)
        return AICERR_BADARGS;

    if (!AIC_ValidIrq(irq))
        return AICERR_BADIRQ;

    line = &base->aib_Lines[irq];

    Disable();

    for (server = line->al_Head; server != NULL && server != interrupt; server = (struct Interrupt *)server->is_Node.ln_Succ)
        prev = server;

    if (server == NULL)
    {
        Enable();
        return AICERR_NOTFOUND;
    }

    if (line->al_Servers == 1)
    {
        AIC_LineOff(base, irq);
        line->al_Stormed = FALSE;
    }

    if (prev != NULL)
        prev->is_Node.ln_Succ = server->is_Node.ln_Succ;
    else
        line->al_Head = (struct Interrupt *)server->is_Node.ln_Succ;
    if (server->is_Node.ln_Succ != NULL)
        server->is_Node.ln_Succ->ln_Pred = (struct Node *)prev;

    server->is_Node.ln_Succ = NULL;
    server->is_Node.ln_Pred = NULL;

    line->al_Servers--;
    base->aib_ServerCount--;

    if (base->aib_ServerCount == 0)
        RemIntServer(INTB_EXTER, &base->aib_Dispatcher);

    Enable();

    return AICERR_OK;
}
