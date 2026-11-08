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

/* Called by CloseLibrary() with the base in A6. Gives the seglist back (to unload the library) if this was the last opener and an
   expunge was waiting for it, else 0. */
ULONG AIC_LibClose(
    REGARG(struct ArmIntcBase *base, "a6"))
{
    if (base->aib_Lib.lib_OpenCnt != 0)
    {
        base->aib_Lib.lib_OpenCnt--;
    }

    if (base->aib_Lib.lib_OpenCnt == 0 && (base->aib_Lib.lib_Flags & LIBF_DELEXP))
    {
        return AIC_LibExpunge(base);
    }

    return 0;
}
