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

/*
    Enables a line that has servers again, after AIC_DisableInt() or the storm guard. No Disable(): the copy of the enables changes
    with one instruction (AIC_LineOn()), so it may be called from an interrupt, a server of the dispatcher included.
*/
LONG L_AIC_EnableInt(
    REGARG(ULONG irq, "d0"),
    REGARG(struct ArmIntcBase *base, "a6"))
{
    struct AicLine *line;

    if (!AIC_ValidIrq(irq))
        return AICERR_BADIRQ;

    line = &base->aib_Lines[irq];

    if (line->al_Head == NULL)
        return AICERR_NOSERVER;

    line->al_Run = 0;
    line->al_Stormed = FALSE;
    AIC_LineOn(base, irq);

    return AICERR_OK;
}
