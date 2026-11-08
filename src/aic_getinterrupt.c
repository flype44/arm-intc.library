/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <proto/devicetree.h>

#include <common/compiler.h>

#include "base.h"

/*
    The number of the line of the index-th entry of a node's "interrupts": <bank line> gives bank * 32 + line. The node's interrupt
    parent must be the ARM control block (two cells an entry, checked when the library started): the entry of another controller
    is refused, a GIC entry <type number flags> would read as a wrong line.
*/
LONG L_AIC_GetInterrupt(
    REGARG(APTR key, "a0"),
    REGARG(ULONG index, "d0"),
    REGARG(struct ArmIntcBase *base, "a6"))
{
    APTR DeviceTreeBase = base->aib_DeviceTreeBase;
    APTR prop;
    const ULONG *cells;
    ULONG bank, line;

    if (key == NULL)
        return AICERR_BADARGS;

    if (AIC_InterruptParent(DeviceTreeBase, key) != base->aib_Phandle)
        return AICERR_NOTOURS;

    prop = DT_FindProperty(key, (CONST_STRPTR)"interrupts");
    if (prop == NULL || index >= DT_GetPropLen(prop) / 8)
        return AICERR_NODT;

    cells = (const ULONG *)DT_GetPropValue(prop) + 2 * index;
    bank = cells[0];
    line = cells[1];

    if (bank >= AIC_BANKS || line >= 32 || (bank == AIC_BANK_ARM && line >= AIC_BANK0_LINES))
        return AICERR_BADIRQ;

    return (LONG)AIC_IRQ(bank, line);
}
