/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <utility/tagitem.h>

#include <common/compiler.h>

#include "base.h"
#include "hw.h"

/* The pending register of each bank */
static const UBYTE bank_pending[AIC_BANKS] = { ARMCTRL_PENDING0, ARMCTRL_PENDING1, ARMCTRL_PENDING2 };

/* An answer about the controller: FALSE for a tag that is not one */
static BOOL ControllerInfo(
    struct ArmIntcBase *base,
    ULONG tag,
    ULONG *answer)
{
    switch (tag)
    {
        case AICI_Version:          *answer = ((ULONG)base->aib_Lib.lib_Version << 16) | base->aib_Lib.lib_Revision; break;
        case AICI_ControllerAddress:*answer = base->aib_Controller; break;
        case AICI_LocalAddress:     *answer = base->aib_Local; break;
        case AICI_GpuRouting:       *answer = base->aib_GpuRouting; break;
        case AICI_StaleLines:       *answer = base->aib_StaleLines; break;
        case AICI_ServerCount:      *answer = base->aib_ServerCount; break;
        case AICI_Passes:           *answer = base->aib_Passes; break;
        case AICI_ForeignPasses:    *answer = base->aib_ForeignPasses; break;
        default:                    return FALSE;
    }

    return TRUE;
}

/* An answer about a line: FALSE for a tag that is not one */
static BOOL LineInfo(
    struct ArmIntcBase *base,
    ULONG irq,
    ULONG tag,
    ULONG *answer)
{
    struct AicLine *line = &base->aib_Lines[irq];
    ULONG bank = irq >> 5;
    ULONG bit = 1UL << (irq & 31);

    switch (tag)
    {
        case AICI_Enabled:      *answer = (base->aib_Enabled[bank] & bit) != 0; break;
        case AICI_Pending:      *answer = (aic_rd(base->aib_Controller, bank_pending[bank]) & bit) != 0; break;
        case AICI_Servers:      *answer = line->al_Servers; break;
        case AICI_Count:        *answer = line->al_Count; break;
        case AICI_Unclaimed:    *answer = line->al_Unclaimed; break;
        case AICI_Stormed:      *answer = line->al_Stormed; break;
        default:                return FALSE;
    }

    return TRUE;
}

/* Answers the questions of the list about the controller (AIC_CONTROLLER) or one line. Only reads: callable from an interrupt. */
ULONG L_AIC_QueryInfoTagList(
    REGARG(ULONG irq, "d0"),
    REGARG(const struct TagItem *tagList, "a0"),
    REGARG(struct ArmIntcBase *base, "a6"))
{
    const struct TagItem *tag;
    ULONG count = 0;

    if (irq != AIC_CONTROLLER && !AIC_ValidIrq(irq))
        return 0;

    while ((tag = AIC_NextTag(&tagList)) != NULL)
    {
        ULONG *answer = (ULONG *)tag->ti_Data;
        BOOL done;

        if (answer == NULL)
            continue;

        done = (irq == AIC_CONTROLLER) ? ControllerInfo(base, tag->ti_Tag, answer) : LineInfo(base, irq, tag->ti_Tag, answer);

        if (done)
            count++;
    }

    return count;
}
