/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <stdint.h>

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/resident.h>

#include <common/compiler.h>

#include "base.h"

/* The first code of the file: started as a program, the library does nothing */
int __attribute__((no_reorder)) _start()
{
    return -1;
}

extern const char libraryEnd;
extern const char libraryName[];
extern const char libraryIdString[];
extern const ULONG InitTable[];

const struct Resident RomTag __attribute__((used)) = {
    RTC_MATCHWORD,
    (struct Resident *)&RomTag,
    (APTR)&libraryEnd,
    RTF_AUTOINIT,
    AIC_VERSION,
    NT_LIBRARY,
    0,
    (char *)((intptr_t)&libraryName),
    (char *)((intptr_t)&libraryIdString),
    (APTR)InitTable,
};

const char libraryName[] = ARMINTCNAME;
const char libraryIdString[] = VERSION_STRING;

/* The reserved vector of every library */
static ULONG ExtFunc(VOID)
{
    return 0;
}

/* The standard four, then the calls in the order of sfd/arm-intc_lib.sfd. A call is added at the END (the revision goes up): the
   vectors that are there keep their offsets. */
static const ULONG aic_functions[] = {
    (ULONG)AIC_LibOpen,
    (ULONG)AIC_LibClose,
    (ULONG)AIC_LibExpunge,
    (ULONG)ExtFunc,
    (ULONG)L_AIC_AddIntServer,
    (ULONG)L_AIC_RemIntServer,
    (ULONG)L_AIC_EnableInt,
    (ULONG)L_AIC_DisableInt,
    (ULONG)L_AIC_GetInterrupt,
    (ULONG)L_AIC_QueryInfoTagList,
    (ULONG)-1
};

const ULONG InitTable[4] = {
    sizeof(struct ArmIntcBase),
    (ULONG)aic_functions,
    0,
    (ULONG)AIC_LibInit
};
