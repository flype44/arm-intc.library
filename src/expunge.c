/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include <common/compiler.h>

#include "base.h"

/*
    Called by the system with the base in A6, to remove the library: when the memory runs short ("Avail FLUSH") or on request. Done at
    once if nobody has it open and no line has a server, else later (the Close of the last opener). Gives the seglist to unload, or 0.
    A server belongs to an opener: with the library closed by everybody, none should be left. One that is left (a driver that did not
    take it off) keeps the library, whose dispatcher still calls it. The reset guard is removed last; it cannot be when a program
    patched ColdReboot() after the library: the chain goes through the guard's code, and the library stays.
*/
ULONG AIC_LibExpunge(
    REGARG(struct ArmIntcBase *base, "a6"))
{
    struct ExecBase *SysBase = base->aib_SysBase;
    BPTR seglist = 0;

    if (base->aib_Lib.lib_OpenCnt == 0 && base->aib_ServerCount == 0 && (!base->aib_Guarded || reset_guard_remove(&base->aib_ResetGuard)))
    {
        ULONG neg = base->aib_Lib.lib_NegSize;
        ULONG pos = base->aib_Lib.lib_PosSize;

        Remove(&base->aib_Lib.lib_Node);

        seglist = base->aib_SegList;
        FreeMem((UBYTE *)base - neg, neg + pos);
    }
    else
    {
        base->aib_Lib.lib_Flags |= LIBF_DELEXP;
    }

    return (ULONG)seglist;
}
