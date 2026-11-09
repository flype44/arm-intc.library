/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/*
    aic-probe: what arm-intc.library relies on, read and proved on the machine, without the library. The code of the library that
    finds the controllers (src/controller.c) and reads a line from a node (src/aic_getinterrupt.c) is linked in and run against a
    base of the probe's own, next to an independent reading of the device tree: both must agree.

      (no keyword)  read only. The device tree (the two controllers, the /soc ranges, the lines of every node), what the library's
                    code finds there, the registers of the ARM control block, of the local controller (GPU routing, IRQ source of
                    core 0) and INTENAR / INTREQR. Nothing is written.
      TIMER         one real line, announced: compare 1 of the system timer (bank 1 line 1, the line 33 of the library), which
                    neither Emu68 nor the firmware uses. First with the line disabled (does a pending register show a line that is
                    not enabled?), then with a server of the probe on INTB_EXTER and the line enabled, for N matches (default 10).
                    The server clears the match; it disables the line itself after 4 * N rings of it. The line is disabled and the
                    server removed at the end, whatever happened.

    Template: TIMER/S,N/N         Output: the console, or a file with >RAM:aic-probe.txt
*/

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/interrupts.h>
#include <exec/libraries.h>
#include <hardware/intbits.h>
#include <dos/dos.h>
#include <workbench/startup.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/devicetree.h>

#include "base.h"
#include "hw.h"

static const char version[] __attribute__((used)) = "$VER: aic-probe 0.4 (09.11.2026)";

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
APTR DeviceTreeBase;

#define Print(fmt, ...)     do { Printf((CONST_STRPTR)(fmt), ##__VA_ARGS__); Flush(Output()); } while (0)

/* The system timer (brcm,bcm2835-system-timer), Linux: drivers/clocksource/bcm2835_timer.c */
#define ST_CS               0x00        /* bit n: compare n matched; write 1 to clear it */
#define ST_CLO              0x04        /* free running counter, 1 MHz, low 32 bits */
#define ST_C1               0x10        /* compare 1 */
#define ST_M1               (1UL << 1)

/* The local controller (Linux: include/linux/irqchip/irq-bcm2836.h) */
#define LOCAL_IRQ_SOURCE0   0x60        /* what rings at core 0; bit 8: the GPU interrupt (the ARM control block) */
#define LOCAL_SRC_GPU       (1UL << 8)
#define LOCAL_IRQ_SOURCE(c) (0x60 + 4 * (c))    /* the IRQ sources of core c */
#define LOCAL_FIQ_SOURCE(c) (0x70 + 4 * (c))    /* the FIQ sources of core c */

#define INTENAR             0xdff01c
#define INTREQR             0xdff01e

/* The base the library's code runs against: only the fields that controller.c and aic_getinterrupt.c use are set */
static struct ArmIntcBase base;

/* What the server sees, shared with the task */
struct Probe
{
    ULONG           controller;
    ULONG           timer;
    ULONG           bit;            /* the bit of the line in bank 1 */
    ULONG           limit;          /* rings of our line after which the server disables it itself */
    volatile ULONG  calls;          /* calls of the server */
    volatile ULONG  hits;           /* calls with our line pending */
    volatile ULONG  when;           /* CLO when it was seen */
    volatile ULONG  still_high;     /* the line still pending after the match was cleared and a nop */
    volatile ULONG  guard;          /* TRUE: the server disabled the line */
};

static struct Probe probe;
static struct Interrupt server;

/* The body of the server, at level 6: non zero when the line of the timer rang (and was cleared) */
ULONG ProbeServe(struct Probe *p)
{
    p->calls++;

    if ((aic_rd(p->controller, ARMCTRL_PENDING1) & p->bit) == 0)
        return 0;

    p->when = aic_rd(p->timer, ST_CLO);
    aic_wr(p->timer, ST_CS, ST_M1);
    p->hits++;

    if (aic_rd(p->controller, ARMCTRL_PENDING1) & p->bit)
        p->still_high++;

    if (p->hits >= p->limit && !p->guard)
    {
        aic_wr(p->controller, ARMCTRL_DISABLE1, p->bit);
        p->guard = TRUE;
    }

    return 1;
}

/* The entry from Exec (A1 = is_Data): Z from the result, as the dispatcher of the library */
ULONG ProbeServer(void);
__asm__(
    "	.text\n"
    "	.even\n"
    "	.globl	_ProbeServer\n"
    "_ProbeServer:\n"
    "	move.l	a1,-(sp)\n"
    "	jsr	_ProbeServe\n"
    "	addq.l	#4,sp\n"
    "	tst.l	d0\n"
    "	rts\n");

static inline UWORD Chip(ULONG reg)
{
    return *(volatile UWORD *)reg;
}

/* Depth first, no recursion: the node after `key` in the walk of the tree under `root`, NULL at the end */
static APTR NextNode(APTR root, APTR key)
{
    APTR next = DT_GetChild(key, NULL);

    while (next == NULL && key != root)
    {
        APTR parent = DT_GetParent(key);

        next = DT_GetChild(parent, key);
        key = parent;
    }

    return (key == root && next == NULL) ? NULL : next;
}

static ULONG Cell(APTR key, const char *name, ULONG fallback)
{
    return AIC_GetCell(DeviceTreeBase, key, (CONST_STRPTR)name, fallback);
}

static BOOL HasString(APTR key, const char *prop_name, const char *s)
{
    APTR prop = key ? DT_FindProperty(key, (CONST_STRPTR)prop_name) : NULL;
    const char *p, *end;

    if (prop == NULL)
        return FALSE;

    p = DT_GetPropValue(prop);
    end = p + DT_GetPropLen(prop);

    while (p < end)
    {
        const char *a = p, *b = s;

        while (*a != 0 && *a == *b)
        {
            a++;
            b++;
        }
        if (*a == *b)
            return TRUE;

        while (p < end && *p != 0)
            p++;
        p++;
    }

    return FALSE;
}

static APTR FindCompatible(const char *name)
{
    APTR root = DT_OpenKey((CONST_STRPTR)"/");
    APTR key;

    for (key = DT_GetChild(root, NULL); key != NULL; key = NextNode(root, key))
        if (HasString(key, "compatible", name))
            break;

    DT_CloseKey(root);

    return key;
}

static APTR FindPhandle(ULONG phandle)
{
    APTR root;
    APTR key;

    /* 0 is no phandle: every node without one would match */
    if (phandle == 0)
        return NULL;

    root = DT_OpenKey((CONST_STRPTR)"/");
    for (key = DT_GetChild(root, NULL); key != NULL; key = NextNode(root, key))
        if (Cell(key, "phandle", 0) == phandle || Cell(key, "linux,phandle", 0) == phandle)
            break;

    DT_CloseKey(root);

    return key;
}

/* A property: its strings when it holds text, else its cells */
static void ShowProp(APTR key, const char *name)
{
    APTR prop = key ? DT_FindProperty(key, (CONST_STRPTR)name) : NULL;
    ULONG len, i;
    const UBYTE *v;
    BOOL text = TRUE;

    if (prop == NULL)
    {
        Print("    %-18s (none)\n", (LONG)name);
        return;
    }

    len = DT_GetPropLen(prop);
    v = DT_GetPropValue(prop);

    for (i = 0; i < len; i++)
        if ((v[i] < 32 || v[i] > 126) && v[i] != 0)
            text = FALSE;
    if (len == 0 || v[len - 1] != 0 || v[0] == 0)
        text = FALSE;

    Printf((CONST_STRPTR)"    %-18s", (LONG)name);
    if (text)
    {
        for (i = 0; i < len; i += 1)
        {
            if (i == 0 || v[i - 1] == 0)
                Printf((CONST_STRPTR)" \"%s\"", (LONG)&v[i]);
        }
    }
    else
    {
        for (i = 0; i + 4 <= len; i += 4)
            Printf((CONST_STRPTR)" %08lx", ((const ULONG *)v)[i / 4]);
        if (len == 0)
            Printf((CONST_STRPTR)" (empty)");
    }
    Print("\n");
}

/* The 68k address of the first "reg" of a node, translated through the "ranges" of its bus: written apart from the library's
   RegAddress(), to check it. 0 when it cannot be translated. */
static ULONG Translate(APTR key)
{
    APTR bus = DT_GetParent(key);
    ULONG ac = Cell(bus, "#address-cells", 2);
    ULONG sc = Cell(bus, "#size-cells", 1);
    ULONG pc = Cell(DT_GetParent(bus), "#address-cells", 2);
    APTR reg = DT_FindProperty(key, (CONST_STRPTR)"reg");
    APTR ranges = DT_FindProperty(bus, (CONST_STRPTR)"ranges");
    const ULONG *r;
    ULONG addr, n, i;

    if (reg == NULL || ranges == NULL || ac == 0 || DT_GetPropLen(reg) < 4 * ac)
        return 0;

    addr = ((const ULONG *)DT_GetPropValue(reg))[ac - 1];
    if (DT_GetPropLen(ranges) == 0)
        return addr;

    r = DT_GetPropValue(ranges);
    n = DT_GetPropLen(ranges) / (4 * (ac + pc + sc));

    for (i = 0; i < n; i++, r += ac + pc + sc)
    {
        ULONG child = r[ac - 1];
        ULONG parent = r[ac + pc - 1];
        ULONG size = r[ac + pc + sc - 1];

        if (addr >= child && addr - child < size)
            return addr - child + parent;
    }

    return 0;
}

static void ShowNode(const char *what, APTR key)
{
    Print("  %s: %s\n", (LONG)what, key ? (LONG)DT_GetKeyName(key) : (LONG)"(not found)");
    if (key == NULL)
        return;

    ShowProp(key, "compatible");
    ShowProp(key, "reg");
    ShowProp(key, "#interrupt-cells");
    ShowProp(key, "interrupt-parent");
    ShowProp(key, "phandle");
    Print("    68k address        %08lx (translated by the probe)\n", Translate(key));
}

static int Verdicts;
static int Failures;

static void Verdict(BOOL ok, const char *what)
{
    Verdicts++;
    if (!ok)
        Failures++;
    Print("  [%s] %s\n", ok ? (LONG)" ok " : (LONG)"FAIL", (LONG)what);
}

static ULONG Elapsed(ULONG timer, ULONG since)
{
    return aic_rd(timer, ST_CLO) - since;
}

static void Wait_us(ULONG timer, ULONG us)
{
    ULONG start = aic_rd(timer, ST_CLO);

    while (Elapsed(timer, start) < us)
        ;
}

/*
    No interrupt came: the state while the line is enabled and its match pending. For 20 ms: does the ARM control block show
    the line, which core gets the GPU interrupt (IRQ or FIQ source registers of the 4 cores), does Emu68 show EXTER in
    INTREQR (it does while an IRQ of core 0 waits for the 68k)?
*/
static void Diagnose(ULONG ctrl, ULONG local, ULONG bit)
{
    ULONG seen_p0 = 0, seen_p1 = 0, seen_irq[4] = { 0, 0, 0, 0 }, seen_fiq[4] = { 0, 0, 0, 0 }, exter = 0, samples = 0;
    ULONG snap[13], c, start;

    /* Everything read before the first Print: a Print lets the system run, and the interrupt may be served meanwhile */
    snap[0] = aic_rd(ctrl, ARMCTRL_PENDING0);
    snap[1] = aic_rd(ctrl, ARMCTRL_PENDING1);
    snap[2] = aic_rd(ctrl, ARMCTRL_ENABLE1);
    snap[3] = aic_rd(local, LOCAL_GPU_ROUTING);
    for (c = 0; c < 4; c++)
    {
        snap[4 + c] = aic_rd(local, LOCAL_IRQ_SOURCE(c));
        snap[8 + c] = aic_rd(local, LOCAL_FIQ_SOURCE(c));
    }
    snap[12] = ((ULONG)Chip(INTENAR) << 16) | Chip(INTREQR);

    Print("  diagnosis, the line enabled and pending (calls of the server so far %ld):\n", probe.calls);
    Print("    PENDING0 %08lx PENDING1 %08lx ENABLE1 %08lx GPU_ROUTING %08lx\n", snap[0], snap[1], snap[2], snap[3]);
    Print("    IRQ sources of cores 0-3 %08lx %08lx %08lx %08lx\n", snap[4], snap[5], snap[6], snap[7]);
    Print("    FIQ sources of cores 0-3 %08lx %08lx %08lx %08lx\n", snap[8], snap[9], snap[10], snap[11]);
    Print("    INTENAR %04lx INTREQR %04lx\n", snap[12] >> 16, snap[12] & 0xffff);

    start = aic_rd(probe.timer, ST_CLO);

    while (aic_rd(probe.timer, ST_CLO) - start < 20000)
    {
        seen_p0 |= aic_rd(ctrl, ARMCTRL_PENDING0);
        seen_p1 |= aic_rd(ctrl, ARMCTRL_PENDING1);
        for (c = 0; c < 4; c++)
        {
            seen_irq[c] |= aic_rd(local, LOCAL_IRQ_SOURCE(c));
            seen_fiq[c] |= aic_rd(local, LOCAL_FIQ_SOURCE(c));
        }
        if (Chip(INTREQR) & INTF_EXTER)
            exter++;
        samples++;
    }

    Print("    in 20 ms (%ld samples), bits seen at least once:\n", samples);
    Print("    PENDING0 %08lx PENDING1 %08lx (our line: %s)\n", seen_p0, seen_p1, (seen_p1 & bit) ? (LONG)"yes" : (LONG)"no");
    Print("    IRQ sources %08lx %08lx %08lx %08lx\n", seen_irq[0], seen_irq[1], seen_irq[2], seen_irq[3]);
    Print("    FIQ sources %08lx %08lx %08lx %08lx\n", seen_fiq[0], seen_fiq[1], seen_fiq[2], seen_fiq[3]);
    Print("    EXTER in INTREQR: %ld of %ld samples; server calls %ld\n", exter, samples, probe.calls);

    for (c = 0; c < 4; c++)
        if ((seen_irq[c] | seen_fiq[c]) & LOCAL_SRC_GPU)
            Print("    the GPU interrupt reaches core %ld as %s\n", c, (seen_irq[c] & LOCAL_SRC_GPU) ? (LONG)"IRQ" : (LONG)"FIQ");
}

/* The active part: compare 1 of the system timer through the ARM control block */
static void TimerTest(APTR timer_node, ULONG timer, LONG count)
{
    ULONG ctrl = base.aib_Controller;
    LONG irq = L_AIC_GetInterrupt(timer_node, 1, &base);
    ULONG bit, en, p0, p1, cs, src, i, done = 0, timeouts = 0, lat_sum = 0, lat_max = 0;

    Print("\n[6] TIMER: compare 1 of the system timer, %ld matches\n", count);

    if (irq != AIC_IRQ(AIC_BANK_GPU1, 1))
    {
        Print("  the second entry of the timer's \"interrupts\" is %ld, not 33: stopped\n", irq);
        Failures++;
        return;
    }

    bit = 1UL << (irq & 31);
    en = aic_rd(ctrl, ARMCTRL_ENABLE1);
    cs = aic_rd(timer, ST_CS);
    Print("  ENABLE1 %08lx, CS %08lx before the test\n", en, cs);

    if (en & bit)
    {
        Print("  line 33 is already enabled: someone uses compare 1, stopped\n");
        Failures++;
        return;
    }

    /* 1. The line disabled: what the registers show of a match */
    aic_wr(timer, ST_CS, ST_M1);
    aic_wr(timer, ST_C1, aic_rd(timer, ST_CLO) + 1000);
    Wait_us(timer, 3000);

    cs = aic_rd(timer, ST_CS);
    p0 = aic_rd(ctrl, ARMCTRL_PENDING0);
    p1 = aic_rd(ctrl, ARMCTRL_PENDING1);
    src = aic_rd(base.aib_Local, LOCAL_IRQ_SOURCE0);
    Print("  disabled, after the match: CS %08lx PENDING0 %08lx PENDING1 %08lx IRQ_SOURCE0 %08lx\n", cs, p0, p1, src);
    Verdict((cs & ST_M1) != 0, "the match is seen in CS (the timer works)");
    Print("  a disabled line in PENDING1: %s; GPU at core 0: %s\n",
        (p1 & bit) ? (LONG)"shown" : (LONG)"not shown", (src & LOCAL_SRC_GPU) ? (LONG)"yes" : (LONG)"no");
    Verdict((src & LOCAL_SRC_GPU) == 0, "a disabled line does not reach core 0");

    aic_wr(timer, ST_CS, ST_M1);
    p1 = aic_rd(ctrl, ARMCTRL_PENDING1);
    Print("  match cleared: PENDING1 %08lx\n", p1);
    Verdict((aic_rd(timer, ST_CS) & ST_M1) == 0, "the match is cleared by writing 1 to CS");

    /* 2. A server, the line enabled */
    probe.controller = ctrl;
    probe.timer = timer;
    probe.bit = bit;
    probe.limit = 4 * count;

    server.is_Node.ln_Type = NT_INTERRUPT;
    server.is_Node.ln_Pri = AIC_DISPATCHER_PRI;
    server.is_Node.ln_Name = (char *)"aic-probe";
    server.is_Data = &probe;
    server.is_Code = (VOID (*)())ProbeServer;

    AddIntServer(INTB_EXTER, &server);
    Print("  server added: INTENAR %04lx\n", (ULONG)Chip(INTENAR));

    aic_wr(ctrl, ARMCTRL_ENABLE1, bit);
    en = aic_rd(ctrl, ARMCTRL_ENABLE1);
    Print("  line enabled: ENABLE1 reads %08lx\n", en);
    Verdict((en & bit) != 0, "ENABLE1 reads back the enabled line (AICI_StaleLines relies on it)");

    for (i = 0; i < (ULONG)count && !probe.guard; i++)
    {
        ULONG hits = probe.hits;
        ULONG start = aic_rd(timer, ST_CLO);
        ULONG target = start + 500 + (i * 37) % 500;

        aic_wr(timer, ST_C1, target);

        /* From now, not from the target: before the match CLO - target is a huge unsigned number */
        while (probe.hits == hits && Elapsed(timer, start) < 100000)
            ;

        if (probe.hits == hits)
        {
            timeouts++;
            break;
        }

        done++;
        lat_sum += probe.when - target;
        if (probe.when - target > lat_max)
            lat_max = probe.when - target;
    }

    /* The line still enabled and its match still pending: where does the interrupt go? */
    if (timeouts)
        Diagnose(ctrl, base.aib_Local, bit);

    /* 3. Put everything back */
    aic_wr(ctrl, ARMCTRL_DISABLE1, bit);
    en = aic_rd(ctrl, ARMCTRL_ENABLE1);
    Wait_us(timer, 2000);
    RemIntServer(INTB_EXTER, &server);
    aic_wr(timer, ST_CS, ST_M1);

    Print("  %ld of %ld matches served, %ld timeout; server calls %ld, ours %ld, still high after clear %ld, guard %s\n",
        done, count, timeouts, probe.calls, probe.hits, probe.still_high, probe.guard ? (LONG)"TRIPPED" : (LONG)"no");
    if (done)
        Print("  latency match -> server: mean %ld us, max %ld us\n", lat_sum / done, lat_max);
    Print("  line disabled: ENABLE1 reads %08lx; INTREQR %04lx\n", en, (ULONG)Chip(INTREQR));

    Verdict(done == (ULONG)count && timeouts == 0, "every match became one level 6 interrupt, served by an INTB_EXTER server");
    Verdict(probe.hits == done, "no extra call of the server with the line pending");
    Verdict(probe.still_high == 0, "the line drops as soon as the device is cleared (a nop is enough)");
    Verdict((en & bit) == 0, "DISABLE1 disables the line, and ENABLE1 says so");
}

int main(int wb, struct WBStartup *msg)
{
    LONG args[2] = { 0, 0 };
    struct RDArgs *rd;
    APTR root, key, armctrl, local, timer_node, mbox;
    ULONG phandle, timer;
    LONG count = 10;

    SysBase = *(struct ExecBase **)4UL;
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37);
    if (DOSBase == NULL || wb)
        return RETURN_FAIL;

    rd = ReadArgs((CONST_STRPTR)"TIMER/S,N/N", args, NULL);
    if (rd == NULL)
    {
        PrintFault(IoErr(), (CONST_STRPTR)"aic-probe");
        CloseLibrary((struct Library *)DOSBase);
        return RETURN_FAIL;
    }
    if (args[1])
        count = *(LONG *)args[1];
    if (count < 1 || count > 10000)
        count = 10;

    Print("aic-probe 0.4: what arm-intc.library relies on\n");

    DeviceTreeBase = OpenResource((CONST_STRPTR)"devicetree.resource");
    if (DeviceTreeBase == NULL)
    {
        Print("no devicetree.resource\n");
        FreeArgs(rd);
        CloseLibrary((struct Library *)DOSBase);
        return RETURN_FAIL;
    }

    /* 1. The machine */
    root = DT_OpenKey((CONST_STRPTR)"/");
    Print("\n[1] devicetree.resource %ld.%ld\n", (ULONG)((struct Library *)DeviceTreeBase)->lib_Version,
        (ULONG)((struct Library *)DeviceTreeBase)->lib_Revision);
    ShowProp(root, "model");
    ShowProp(root, "compatible");
    ShowProp(root, "#address-cells");
    ShowProp(root, "interrupt-parent");
    key = DT_OpenKey((CONST_STRPTR)"/chosen");
    ShowProp(key, "bootargs");
    if (key)
        DT_CloseKey(key);

    /* 2. The controllers, read by the probe alone */
    Print("\n[2] The controllers in the device tree\n");
    phandle = Cell(root, "interrupt-parent", 0);
    armctrl = phandle ? FindPhandle(phandle) : NULL;
    ShowNode("interrupt parent of /", armctrl);
    local = armctrl ? FindPhandle(Cell(armctrl, "interrupt-parent", 0)) : NULL;
    ShowNode("its own interrupt parent", local);
    key = DT_OpenKey((CONST_STRPTR)"/soc");
    Print("  /soc\n");
    ShowProp(key, "#address-cells");
    ShowProp(key, "#size-cells");
    ShowProp(key, "ranges");
    DT_CloseKey(key);
    DT_CloseKey(root);

    Verdict(HasString(armctrl, "compatible", "brcm,bcm2836-armctrl-ic"), "the interrupt parent of / is brcm,bcm2836-armctrl-ic");
    Verdict(Cell(armctrl, "#interrupt-cells", 0) == 2, "with 2 cells an interrupt");
    Verdict(HasString(local, "compatible", "brcm,bcm2836-l1-intc"), "its own parent is brcm,bcm2836-l1-intc");
    Verdict(armctrl != NULL && Translate(armctrl) != 0 && local != NULL && Translate(local) != 0,
        "both are mapped for the 68k through the /soc ranges");

    if (Failures)
    {
        Print("\nstopped before reading a register: %ld of %ld checks failed\n", (LONG)Failures, (LONG)Verdicts);
        FreeArgs(rd);
        CloseLibrary((struct Library *)DOSBase);
        return RETURN_WARN;
    }

    /* 3. The library's own code. It reads one register: GPU_ROUTING of the local controller. */
    Print("\n[3] AIC_FindController() of the library (reads LOCAL + 0x0c)\n");
    base.aib_SysBase = SysBase;
    base.aib_DeviceTreeBase = DeviceTreeBase;
    Verdict(AIC_FindController(&base), "AIC_FindController() accepts the machine");
    Print("  controller %08lx, local %08lx, phandle %08lx, GPU routing %08lx\n",
        base.aib_Controller, base.aib_Local, base.aib_Phandle, base.aib_GpuRouting);
    Verdict(base.aib_Controller == Translate(armctrl) && base.aib_Local == Translate(local),
        "the library and the probe find the same addresses");
    Verdict((base.aib_GpuRouting & 3) == 0, "the GPU IRQ goes to core 0");

    if (base.aib_Controller == 0 || base.aib_Local == 0)
    {
        Print("\nstopped: no address\n");
        FreeArgs(rd);
        CloseLibrary((struct Library *)DOSBase);
        return RETURN_WARN;
    }

    /* 4. The registers, read only */
    Print("\n[4] Registers\n");
    Print("  ARM control  PENDING0 %08lx PENDING1 %08lx PENDING2 %08lx FIQ %08lx\n",
        aic_rd(base.aib_Controller, ARMCTRL_PENDING0), aic_rd(base.aib_Controller, ARMCTRL_PENDING1),
        aic_rd(base.aib_Controller, ARMCTRL_PENDING2), aic_rd(base.aib_Controller, ARMCTRL_FIQ_CONTROL));
    Print("               ENABLE0  %08lx ENABLE1  %08lx ENABLE2  %08lx (as they read)\n",
        aic_rd(base.aib_Controller, ARMCTRL_ENABLE0), aic_rd(base.aib_Controller, ARMCTRL_ENABLE1),
        aic_rd(base.aib_Controller, ARMCTRL_ENABLE2));
    Print("  local        GPU_ROUTING %08lx IRQ_SOURCE0 %08lx\n",
        aic_rd(base.aib_Local, LOCAL_GPU_ROUTING), aic_rd(base.aib_Local, LOCAL_IRQ_SOURCE0));
    Print("  chipset      INTENAR %04lx INTREQR %04lx\n", (ULONG)Chip(INTENAR), (ULONG)Chip(INTREQR));

    /* 5. The lines of every node whose interrupt parent is the ARM control block, through the library's AIC_GetInterrupt() */
    Print("\n[5] Lines in the device tree (AIC_GetInterrupt() of the library)\n");
    root = DT_OpenKey((CONST_STRPTR)"/");
    for (key = DT_GetChild(root, NULL); key != NULL; key = NextNode(root, key))
    {
        APTR prop = DT_FindProperty(key, (CONST_STRPTR)"interrupts");
        ULONG n, i;

        if (prop == NULL || AIC_InterruptParent(DeviceTreeBase, key) != base.aib_Phandle)
            continue;

        n = DT_GetPropLen(prop) / 8;
        Printf((CONST_STRPTR)"  %-32s", (LONG)DT_GetKeyName(key));
        for (i = 0; i < n; i++)
            Printf((CONST_STRPTR)" %ld", L_AIC_GetInterrupt(key, i, &base));
        Print("\n");
    }
    DT_CloseKey(root);

    mbox = FindCompatible("brcm,bcm2835-mbox");
    timer_node = FindCompatible("brcm,bcm2835-system-timer");
    Verdict(mbox != NULL && L_AIC_GetInterrupt(mbox, 0, &base) == AIC_IRQ_ARM_MAILBOX, "the mailbox is line 1");
    Verdict(timer_node != NULL && L_AIC_GetInterrupt(timer_node, 1, &base) == AIC_IRQ(AIC_BANK_GPU1, 1),
        "compare 1 of the system timer is line 33");
    Verdict(L_AIC_GetInterrupt(NULL, 0, &base) == AICERR_BADARGS, "AIC_GetInterrupt(NULL) is refused");

    timer = timer_node ? Translate(timer_node) : 0;
    Print("  system timer at %08lx, CLO %08lx\n", timer, timer ? aic_rd(timer, ST_CLO) : 0);

    /* 6. The real line, on request */
    if (args[0])
    {
        if (timer == 0)
            Print("\nno system timer: no TIMER test\n");
        else
            TimerTest(timer_node, timer, count);
    }

    Print("\n%ld checks, %ld failed\n", (LONG)Verdicts, (LONG)Failures);

    FreeArgs(rd);
    CloseLibrary((struct Library *)DOSBase);

    return Failures ? RETURN_WARN : RETURN_OK;
}
