/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>

#include <common/compiler.h>

#include "base.h"

/* Disables a line and keeps its servers. Callable from an interrupt, as AIC_EnableInt(). */
LONG L_AIC_DisableInt(
    REGARG(ULONG irq, "d0"),
    REGARG(struct ArmIntcBase *base, "a6"))
{
    if (!AIC_ValidIrq(irq))
        return AICERR_BADIRQ;

    if (base->aib_Lines[irq].al_Head == NULL)
        return AICERR_NOSERVER;

    AIC_LineOff(base, irq);

    return AICERR_OK;
}
