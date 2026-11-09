/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifndef _BASE_H
#define _BASE_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/interrupts.h>
#include <dos/dos.h>
#include <utility/tagitem.h>

#include <common/compiler.h>

#include <libraries/arm-intc.h>

#include <reset_guard.h>

/* The version comes from project() in CMakeLists.txt, which defines AIC_VERSION and AIC_REVISION. These are only for a build that does
   not go through CMake: keep them equal to project(). */
#ifndef AIC_VERSION
#define AIC_VERSION         0
#endif
#ifndef AIC_REVISION
#define AIC_REVISION        3
#endif

#define AIC_BANKS           3           /* bank 0: the ARM lines, banks 1 and 2: the GPU lines */
#define AIC_BANK0_LINES     8           /* bank 0 has lines 0 to 7 only */
#define AIC_STORM_LIMIT     1000        /* unclaimed rings in a row before the storm guard disables a line */
#define AIC_DISPATCHER_PRI  100         /* the place of the dispatcher on the INTB_EXTER chain, as gic400.library */

/* A line of the controller. Lines 8 to 31 do not exist and stay empty. */
struct AicLine
{
    struct Interrupt *  al_Head;        /* the first server, the others follow through is_Node.ln_Succ; NULL: no server */
    ULONG               al_Count;       /* the times it rang */
    ULONG               al_Unclaimed;   /* the times it rang and no server claimed it */
    UWORD               al_Run;         /* unclaimed rings in a row, for the storm guard */
    UBYTE               al_Servers;
    UBYTE               al_Stormed;     /* TRUE: disabled by the storm guard */
};

struct ArmIntcBase
{
    struct Library      aib_Lib;
    UWORD               aib_Pad;
    struct ExecBase *   aib_SysBase;
    BPTR                aib_SegList;
    APTR                aib_DeviceTreeBase;

    ULONG               aib_Phandle;        /* phandle of the ARM control block: AIC_GetInterrupt() compares a node's parent with it */
    ULONG               aib_Controller;     /* where the 68k sees the registers of the ARM control block */
    ULONG               aib_Local;          /* where the 68k sees the local controller of the cores */
    ULONG               aib_GpuRouting;     /* LOCAL_GPU_ROUTING as it was read at the start */
    ULONG               aib_StaleLines;     /* lines found enabled at the start, left as they were */

    ULONG               aib_Enabled[AIC_BANKS]; /* the lines this library enabled, one bit each, as the enable registers */
    ULONG               aib_ServerCount;
    ULONG               aib_Passes;         /* level 6 interrupts in which one of our lines rang */
    ULONG               aib_ForeignPasses;  /* level 6 interrupts in which none did */

    struct Interrupt    aib_Dispatcher;     /* the INTB_EXTER server, on the chain while a line has a server */

    struct reset_guard  aib_ResetGuard;     /* emu68-common: AIC_PrepareReset() before a reset of AmigaOS */
    BOOL                aib_Guarded;        /* TRUE once the reset guard is installed (by the first server), until the expunge */

    struct AicLine      aib_Lines[AIC_IRQ_COUNT];
};

/* A number that names a line: below AIC_IRQ_COUNT, and not 8 to 31 (bank 0 has 8 lines) */
static inline BOOL AIC_ValidIrq(ULONG irq)
{
    return irq < AIC_IRQ_COUNT && (irq < AIC_BANK0_LINES || irq >= 32);
}

/* Library */
struct ArmIntcBase * AIC_LibInit(REGARG(struct ArmIntcBase *base, "d0"), REGARG(BPTR seglist, "a0"), REGARG(struct ExecBase *SysBase, "a6"));
struct ArmIntcBase * AIC_LibOpen(REGARG(ULONG version, "d0"), REGARG(struct ArmIntcBase *base, "a6"));
ULONG AIC_LibClose(REGARG(struct ArmIntcBase *base, "a6"));
ULONG AIC_LibExpunge(REGARG(struct ArmIntcBase *base, "a6"));

/* The calls of sfd/arm-intc_lib.sfd */
LONG L_AIC_AddIntServer(REGARG(ULONG irq, "d0"), REGARG(struct Interrupt *interrupt, "a1"), REGARG(struct ArmIntcBase *base, "a6"));
LONG L_AIC_RemIntServer(REGARG(ULONG irq, "d0"), REGARG(struct Interrupt *interrupt, "a1"), REGARG(struct ArmIntcBase *base, "a6"));
LONG L_AIC_EnableInt(REGARG(ULONG irq, "d0"), REGARG(struct ArmIntcBase *base, "a6"));
LONG L_AIC_DisableInt(REGARG(ULONG irq, "d0"), REGARG(struct ArmIntcBase *base, "a6"));
LONG L_AIC_GetInterrupt(REGARG(APTR key, "a0"), REGARG(ULONG index, "d0"), REGARG(struct ArmIntcBase *base, "a6"));
ULONG L_AIC_QueryInfoTagList(REGARG(ULONG irq, "d0"), REGARG(const struct TagItem *tagList, "a0"), REGARG(struct ArmIntcBase *base, "a6"));

/* The controllers in the device tree, and the lines (controller.c) */
BOOL AIC_FindController(struct ArmIntcBase *base);
ULONG AIC_CountEnabled(struct ArmIntcBase *base);
void AIC_LineOn(struct ArmIntcBase *base, ULONG irq);
void AIC_LineOff(struct ArmIntcBase *base, ULONG irq);
void AIC_PrepareReset(APTR user);

/* The dispatcher (dispatch.c): AIC_Dispatcher() and AIC_CallServer() are assembly, see there */
ULONG AIC_Dispatcher(void);
ULONG AIC_Dispatch(struct ArmIntcBase *base);
ULONG AIC_CallServer(struct Interrupt *interrupt, ULONG irq, struct ExecBase *SysBase);
ULONG AIC_InterruptParent(APTR DeviceTreeBase, APTR key);
ULONG AIC_GetCell(APTR DeviceTreeBase, APTR key, CONST_STRPTR name, ULONG fallback);

/* TagLists (tags.c) */
const struct TagItem *AIC_NextTag(const struct TagItem **list);

/* Debug output through exec.library/RawPutChar() (init.c), with -DAIC_DEBUG */
void kprintf(REGARG(const char *msg, "a0"), REGARG(void *args, "a1"));

#ifdef AIC_DEBUG
#define bug(string, ...) \
    do { ULONG args[] = {0, __VA_ARGS__}; kprintf(string, &args[1]); } while (0)
#else
#define bug(string, ...) do { } while (0)
#endif

#endif /* _BASE_H */
