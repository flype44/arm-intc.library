/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <exec/libraries.h>

#include <common/compiler.h>

#include "base.h"

/* Called by OpenLibrary() with the base in A6, while the system is forbidden. One base for everybody; nothing is allocated: nothing
   can fail. */
struct ArmIntcBase *AIC_LibOpen(
    REGARG(ULONG version, "d0"),
    REGARG(struct ArmIntcBase *base, "a6"))
{
    (void)version;

    base->aib_Lib.lib_OpenCnt++;
    base->aib_Lib.lib_Flags &= ~LIBF_DELEXP;

    return base;
}
