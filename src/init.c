/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include <common/compiler.h>

#include "base.h"

extern const char libraryName[];
extern const char libraryIdString[];

/*
    Called once by InitResident(), with the base that MakeLibrary() has just made: from the first OpenLibrary() for the library in
    LIBS:, at the start of the system for the library in the ROM. It finds the two controllers in the device tree: where they are
    not (a BCM2711, which has a GIC), the base is given back and OpenLibrary() fails.
*/
struct ArmIntcBase *AIC_LibInit(
    REGARG(struct ArmIntcBase *base, "d0"),
    REGARG(BPTR seglist, "a0"),
    REGARG(struct ExecBase *SysBase, "a6"))
{
    base->aib_Lib.lib_Node.ln_Type = NT_LIBRARY;
    base->aib_Lib.lib_Node.ln_Pri = 0;
    base->aib_Lib.lib_Node.ln_Name = (STRPTR)libraryName;
    base->aib_Lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->aib_Lib.lib_Version = AIC_VERSION;
    base->aib_Lib.lib_Revision = AIC_REVISION;
    base->aib_Lib.lib_IdString = (STRPTR)libraryIdString;

    base->aib_SysBase = SysBase;
    base->aib_SegList = seglist;

    base->aib_DeviceTreeBase = OpenResource((CONST_STRPTR)"devicetree.resource");

    if (base->aib_DeviceTreeBase == NULL || !AIC_FindController(base))
    {
        ULONG neg = base->aib_Lib.lib_NegSize;
        ULONG pos = base->aib_Lib.lib_PosSize;

        FreeMem((UBYTE *)base - neg, neg + pos);
        return NULL;
    }

    /* No line of a session before (a warm reboot) stays enabled with nobody to serve it */
    base->aib_StaleLines = AIC_CleanSlate(base);

    /* The INTB_EXTER server: added to the chain with the first server of a line, taken off with the last one */
    base->aib_Dispatcher.is_Node.ln_Type = NT_INTERRUPT;
    base->aib_Dispatcher.is_Node.ln_Pri = AIC_DISPATCHER_PRI;
    base->aib_Dispatcher.is_Node.ln_Name = (STRPTR)libraryName;
    base->aib_Dispatcher.is_Data = base;
    base->aib_Dispatcher.is_Code = (VOID (*)())AIC_Dispatcher;

    return base;
}

/* The debug output of the library goes through exec.library/RawPutChar() only: whoever listens there gets it (Sashimi, or "rxnet debug").
   RawDoFmt() is given a procedure of its own and not NULL: with NULL it writes into the memory that PutChData points to. SysBase travels
   in PutChData (A3), so that nothing is kept in a global. */
static void putch(REGARG(UBYTE data, "d0"), REGARG(struct ExecBase *SysBase, "a3"))
{
    register struct ExecBase *sysbase __asm("a6") = SysBase;
    register ULONG character __asm("d0") = data;

    /* exec.library/RawPutChar(), LVO -516 (the headers of this toolchain do not declare it); d0, d1, a0 and a1 are scratch */
    __asm volatile ("jsr -516(%%a6)"
        : "+r" (character)
        : "r" (sysbase)
        : "d1", "a0", "a1", "cc", "memory");
}

void kprintf(REGARG(const char *msg, "a0"), REGARG(void *args, "a1"))
{
    struct ExecBase *SysBase = *(struct ExecBase **)4UL;

    RawDoFmt((CONST_STRPTR)msg, args, (APTR)putch, SysBase);
}
