/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

/*
    aic-test: arm-intc.library on the machine, through its calls only, with compare 1 of the system timer (line 33) as the line
    under test. The library is opened from a file (LIB=, a path: nothing is installed in LIBS:).

      1. open, the answers about the controller; no reset guard yet; the open changed no enable register (lines that other
         drivers enabled stay enabled)
      2. bad calls: each error code of the Autodoc
      3. one server, N matches: each one served, counted, claimed; the first server installs the reset guard (ColdReboot() patched)
      4. a chain: a server of higher priority that never claims runs first, the one that claims after it; a third one of the
         highest priority that claims ends the walk before the others
      5. AIC_DisableInt() holds a match back, AIC_EnableInt() lets it ring
      6. the last server out: the line is disabled, the dispatcher leaves INTB_EXTER, a match rings nobody
      7. STORM only: a server that never claims nor clears the device; the storm guard must disable the line, and
         AIC_EnableInt() must give it back
      VBLANK only: the vertical blank of the HDMI output, as VideoCore.card would take it on a VC4: pixelvalve 2
         (brcm,bcm2835-pixelvalve2, line 74), PV_INT_VFP_START, for 2 seconds. Skipped when the pixelvalve does not run.
      8. the library expunged (as Avail FLUSH), and the expunge gave ColdReboot() back; line 33 enabled by hand, as another
         driver would, the library opened again: it counts the line (AICI_StaleLines one more) and leaves it enabled
      REBOOT only, after every step passed: a server on line 33, then ColdReboot(). After the restart, "aic-probe" must show
         ENABLE1 without line 33 (bit 1): the reset guard disabled it before the reset

    SHARED: the library already in memory, opened by another driver (dwc2usb.device and its USB line 41): it is opened by its
      name, not loaded from LIB; the counts of step 1, 3 and 6 are taken from the servers already there; no expunge (step 8)
      and no REBOOT. At the end the lines of the other driver must still have their servers, be enabled, not stormed, and
      their count must have gone up (they kept ringing meanwhile).

    Two more, alone (no test, LIB is not opened), for the counter-proof of REBOOT:
      CONTROL: line 33 enabled by hand, no library, then ColdReboot(). After the restart aic-probe must show bit 1 of ENABLE1:
         without the guard, a line survives the reset
      LINEOFF: line 33 disabled by hand (after CONTROL)

    Template: LIB/A,N/N,STORM/S,VBLANK/S,REBOOT/S,CONTROL/S,LINEOFF/S,SHARED/S
*/

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/interrupts.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <workbench/startup.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/devicetree.h>
#include <proto/arm-intc.h>

#include "hw.h"

static const char version[] __attribute__((used)) = "$VER: aic-test 0.5 (09.11.2026)";

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
APTR DeviceTreeBase;
struct Library *ArmIntcBase;

/* SHARED: the servers of the other drivers when the test starts */
static ULONG BaseServers;

#define Print(fmt, ...)     do { Printf((CONST_STRPTR)(fmt), ##__VA_ARGS__); Flush(Output()); } while (0)

#define ST_CS               0x00
#define ST_CLO              0x04
#define ST_C1               0x10
#define ST_M1               (1UL << 1)

#define LINE                33          /* compare 1 of the system timer: bank 1, line 1 */
#define ALL_LINES           96          /* SHARED: the lines read for the servers of other drivers */

/* The pixelvalve (Linux: drivers/gpu/drm/vc4/vc4_regs.h; the same registers as on the BCM2711, where VideoCore.card uses them) */
#define PV_CONTROL          0x00
#define PV_CONTROL_EN       (1UL << 0)
#define PV_INTEN            0x24
#define PV_INTSTAT          0x28        /* write 1 to clear */
#define PV_INT_VFP_START    (1UL << 7)  /* start of the vertical front porch: the picture is over */

/* A server of the test: what it does, and what it saw */
enum { CLAIM, PASS, STORM, VBLANK };

struct Srv
{
    ULONG           mode;
    ULONG           timer;
    volatile ULONG  calls;
    volatile ULONG  hits;           /* matches it cleared and claimed */
    volatile ULONG  when;           /* CLO when it claimed the last one */
    volatile ULONG  seq;            /* the order of its last call among all the calls of the servers */
    volatile ULONG  irq;            /* D0 of its last call: the line */
    ULONG           device;         /* VBLANK: the pixelvalve */
    volatile ULONG  min, max;       /* VBLANK: the shortest and longest time between two, in us */
};

static volatile ULONG Seq;
static struct Srv srv_a, srv_b, srv_c, srv_s, srv_v;
static struct Interrupt int_a, int_b, int_c, int_s, int_v;

/* At level 6, called by the library's dispatcher with A1 = is_Data and D0 = the line: non zero claims the interrupt */
ULONG TestServe(struct Srv *s, ULONG irq)
{
    s->calls++;
    s->seq = ++Seq;
    s->irq = irq;

    if (s->mode == VBLANK)
    {
        ULONG now, delta;

        if ((aic_rd(s->device, PV_INTSTAT) & PV_INT_VFP_START) == 0)
            return 0;

        aic_wr(s->device, PV_INTSTAT, PV_INT_VFP_START);
        now = aic_rd(s->timer, ST_CLO);
        delta = now - s->when;
        if (s->hits != 0 && delta < s->min)
            s->min = delta;
        if (s->hits != 0 && delta > s->max)
            s->max = delta;
        s->when = now;
        s->hits++;

        return 1;
    }

    if (s->mode != CLAIM)
        return 0;

    if ((aic_rd(s->timer, ST_CS) & ST_M1) == 0)
        return 0;

    s->when = aic_rd(s->timer, ST_CLO);
    aic_wr(s->timer, ST_CS, ST_M1);
    s->hits++;

    return 1;
}

/* The entry from the library: D0 and A1 to the C body, Z from its result */
ULONG TestServer(void);
__asm__(
    "	.text\n"
    "	.even\n"
    "	.globl	_TestServer\n"
    "_TestServer:\n"
    "	move.l	d0,-(sp)\n"
    "	move.l	a1,-(sp)\n"
    "	jsr	_TestServe\n"
    "	addq.l	#8,sp\n"
    "	tst.l	d0\n"
    "	rts\n");

static ULONG Timer;
static int Checks, Failures;

static void Check(BOOL ok, const char *what)
{
    Checks++;
    if (!ok)
        Failures++;
    Print("  [%s] %s\n", ok ? (LONG)" ok " : (LONG)"FAIL", (LONG)what);
}

static void CheckValue(LONG got, LONG want, const char *what)
{
    Checks++;
    if (got != want)
        Failures++;
    Print("  [%s] %s: %ld (expected %ld)\n", got == want ? (LONG)" ok " : (LONG)"FAIL", (LONG)what, got, want);
}

static ULONG Now(void)
{
    return aic_rd(Timer, ST_CLO);
}

static void Wait_us(ULONG us)
{
    ULONG start = Now();

    while (Now() - start < us)
        ;
}

/* A match of compare 1 in `us` microseconds */
static ULONG Fire(ULONG us)
{
    ULONG target = Now() + us;

    aic_wr(Timer, ST_C1, target);

    return target;
}

/* Waits until the server has claimed more than `old`, `us` at most; TRUE if it did */
static BOOL WaitHits(struct Srv *s, ULONG old, ULONG us)
{
    ULONG start = Now();

    while (s->hits == old && Now() - start < us)
        ;

    return s->hits != old;
}

/* As "Avail FLUSH": an allocation that cannot succeed makes the system expunge what nobody uses, and ramlib unloads it */
static void FlushMemory(void)
{
    int i;

    for (i = 0; i < 3; i++)
    {
        APTR huge = AllocMem(0x7ffffff0UL, MEMF_PUBLIC);

        if (huge)
            FreeMem(huge, 0x7ffffff0UL);
    }
}

static ULONG Query(ULONG irq, ULONG tag)
{
    struct TagItem tags[2];
    ULONG answer = 0xdeadbeef;

    tags[0].ti_Tag = tag;
    tags[0].ti_Data = (ULONG)&answer;
    tags[1].ti_Tag = TAG_DONE;
    tags[1].ti_Data = 0;

    AIC_QueryInfoTagList(irq, tags);

    return answer;
}

static void InitServer(struct Interrupt *in, struct Srv *s, ULONG mode, BYTE pri, const char *name)
{
    s->mode = mode;
    s->timer = Timer;
    s->calls = s->hits = s->when = s->seq = s->irq = 0;

    in->is_Node.ln_Type = NT_INTERRUPT;
    in->is_Node.ln_Pri = pri;
    in->is_Node.ln_Name = (char *)name;
    in->is_Data = s;
    in->is_Code = (VOID (*)())TestServer;
}

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

static BOOL HasCompatible(APTR key, const char *s)
{
    APTR prop = DT_FindProperty(key, (CONST_STRPTR)"compatible");
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
        if (HasCompatible(key, name))
            break;

    DT_CloseKey(root);

    return key;
}

/* Where the 68k sees a node of /soc: its "reg" through the /soc ranges (as aic-probe, which proved it) */
static ULONG SocAddress(APTR key)
{
    APTR bus = DT_GetParent(key);
    APTR reg = DT_FindProperty(key, (CONST_STRPTR)"reg");
    APTR ranges = DT_FindProperty(bus, (CONST_STRPTR)"ranges");
    const ULONG *r;
    ULONG addr, n, i;

    if (reg == NULL || ranges == NULL || DT_GetPropLen(ranges) == 0)
        return 0;

    /* /soc of the BCM2837: one cell for each address and size on both sides (aic-probe [2]) */
    addr = *(const ULONG *)DT_GetPropValue(reg);
    r = DT_GetPropValue(ranges);
    n = DT_GetPropLen(ranges) / 12;

    for (i = 0; i < n; i++, r += 3)
        if (addr >= r[0] && addr - r[0] < r[2])
            return addr - r[0] + r[1];

    return 0;
}

/* The library from a file: an absolute path, so that ramlib finds it from its own process */
static struct Library *OpenFromFile(const char *path, char *buffer, ULONG size)
{
    BPTR lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    BOOL named;

    if (lock == 0)
        return NULL;

    named = NameFromLock(lock, (STRPTR)buffer, size);
    UnLock(lock);

    return named ? OpenLibrary((CONST_STRPTR)buffer, 0) : NULL;
}

/* Where the vector of exec.library/ColdReboot() jumps (JMP abs.l: the address follows the opcode) */
#define LVO_COLDREBOOT      (-726)

static APTR ColdRebootVector(void)
{
    return *(APTR *)((UBYTE *)SysBase + LVO_COLDREBOOT + 2);
}

static struct Library *FindLibrary(void)
{
    struct Library *lib;

    Forbid();
    lib = (struct Library *)FindName(&SysBase->LibList, (CONST_STRPTR)ARMINTCNAME);
    Permit();

    return lib;
}

/* 3. One server, N matches */
static void OneServer(LONG count)
{
    ULONG i, served = 0, lat_sum = 0, lat_max = 0;
    ULONG passes = Query(AIC_CONTROLLER, AICI_Passes);
    /* The counts of a line live as long as the library: SHARED finds those of the runs before */
    ULONG count0 = Query(LINE, AICI_Count), unclaimed0 = Query(LINE, AICI_Unclaimed);

    Print("\n[3] one server on line %ld, %ld matches\n", (LONG)LINE, count);

    InitServer(&int_a, &srv_a, CLAIM, 0, "aic-test A");
    aic_wr(Timer, ST_CS, ST_M1);
    CheckValue(AIC_AddIntServer(LINE, &int_a), AICERR_OK, "AIC_AddIntServer()");
    CheckValue(AIC_AddIntServer(LINE, &int_a), AICERR_OK, "the same server again (no change)");
    CheckValue(Query(AIC_CONTROLLER, AICI_ResetGuard), TRUE, "AICI_ResetGuard after the first server");
    CheckValue(Query(LINE, AICI_Servers), 1, "AICI_Servers");
    CheckValue(Query(LINE, AICI_Enabled), TRUE, "AICI_Enabled");
    CheckValue(Query(AIC_CONTROLLER, AICI_ServerCount), BaseServers + 1, "AICI_ServerCount");

    for (i = 0; i < (ULONG)count; i++)
    {
        ULONG old = srv_a.hits;
        ULONG target = Fire(300 + (i * 37) % 700);

        if (!WaitHits(&srv_a, old, 100000))
            break;

        served++;
        lat_sum += srv_a.when - target;
        if (srv_a.when - target > lat_max)
            lat_max = srv_a.when - target;
    }

    CheckValue(served, count, "matches served");
    if (served)
        Print("  latency match -> server: mean %ld us, max %ld us\n", lat_sum / served, lat_max);
    CheckValue(srv_a.irq, LINE, "D0 of the server is the line");
    CheckValue(Query(LINE, AICI_Count) - count0, served, "AICI_Count, more by");
    CheckValue(Query(LINE, AICI_Unclaimed) - unclaimed0, 0, "AICI_Unclaimed, more by");
    Check(Query(AIC_CONTROLLER, AICI_Passes) - passes >= served, "AICI_Passes went up by the matches at least");
    Print("  server calls %ld, passes %ld, foreign passes %ld\n", srv_a.calls,
        Query(AIC_CONTROLLER, AICI_Passes) - passes, Query(AIC_CONTROLLER, AICI_ForeignPasses));
}

/* 4. The chain: priorities and the end of the walk */
static void Chain(LONG count)
{
    ULONG i, ok = 0;

    Print("\n[4] a chain on line %ld\n", (LONG)LINE);

    InitServer(&int_b, &srv_b, PASS, 10, "aic-test B");
    CheckValue(AIC_AddIntServer(LINE, &int_b), AICERR_OK, "a second server, priority 10, never claims");
    CheckValue(Query(LINE, AICI_Servers), 2, "AICI_Servers");

    srv_a.calls = srv_a.hits = 0;
    for (i = 0; i < (ULONG)count; i++)
    {
        ULONG old = srv_a.hits;

        Fire(300);
        if (WaitHits(&srv_a, old, 100000) && srv_b.seq < srv_a.seq)
            ok++;
    }
    CheckValue(ok, count, "each match: B (priority 10) called, then A (0) claims");
    CheckValue(srv_b.irq, LINE, "D0 of B is the line");

    InitServer(&int_c, &srv_c, CLAIM, 20, "aic-test C");
    CheckValue(AIC_AddIntServer(LINE, &int_c), AICERR_OK, "a third server, priority 20, claims");
    srv_a.calls = srv_b.calls = 0;
    for (i = 0, ok = 0; i < (ULONG)count; i++)
    {
        ULONG old = srv_c.hits;

        Fire(300);
        if (WaitHits(&srv_c, old, 100000))
            ok++;
    }
    CheckValue(ok, count, "C claims every match");
    CheckValue(srv_a.calls + srv_b.calls, 0, "calls of A and B once C claims (the walk ends)");

    CheckValue(AIC_RemIntServer(LINE, &int_c), AICERR_OK, "C removed");
    CheckValue(AIC_RemIntServer(LINE, &int_b), AICERR_OK, "B removed");
    CheckValue(AIC_RemIntServer(LINE, &int_b), AICERR_NOTFOUND, "B removed again");
    CheckValue(Query(LINE, AICI_Servers), 1, "AICI_Servers");
    CheckValue(Query(LINE, AICI_Enabled), TRUE, "the line stays enabled while A is there");
}

/* 5. Disable and enable */
static void DisableEnable(void)
{
    ULONG old = srv_a.hits;

    Print("\n[5] AIC_DisableInt() and AIC_EnableInt()\n");

    CheckValue(AIC_DisableInt(LINE), AICERR_OK, "AIC_DisableInt()");
    CheckValue(Query(LINE, AICI_Enabled), FALSE, "AICI_Enabled");
    Fire(300);
    Check(!WaitHits(&srv_a, old, 20000), "a match while disabled is not served");
    Print("  AICI_Pending while disabled: %ld\n", Query(LINE, AICI_Pending));
    CheckValue(AIC_EnableInt(LINE), AICERR_OK, "AIC_EnableInt()");
    Check(WaitHits(&srv_a, old, 20000), "the match held back is served once enabled");
    CheckValue(Query(LINE, AICI_Pending), FALSE, "AICI_Pending after it");
}

/* 6. The last server out */
static void LastOut(void)
{
    ULONG passes, count;

    Print("\n[6] the last server out\n");

    CheckValue(AIC_RemIntServer(LINE, &int_a), AICERR_OK, "A removed");
    CheckValue(Query(LINE, AICI_Enabled), FALSE, "AICI_Enabled");
    CheckValue(Query(AIC_CONTROLLER, AICI_ServerCount), BaseServers, "AICI_ServerCount");
    CheckValue(AIC_EnableInt(LINE), AICERR_NOSERVER, "AIC_EnableInt() with no server");

    passes = Query(AIC_CONTROLLER, AICI_Passes) + Query(AIC_CONTROLLER, AICI_ForeignPasses);
    count = Query(LINE, AICI_Count);
    Fire(300);
    Wait_us(20000);
    CheckValue(Query(LINE, AICI_Count), count, "AICI_Count of a match with no server");
    /* With the servers of another driver the dispatcher stays in INTB_EXTER */
    if (BaseServers == 0)
        CheckValue(Query(AIC_CONTROLLER, AICI_Passes) + Query(AIC_CONTROLLER, AICI_ForeignPasses), passes,
            "dispatcher calls with no server (it left INTB_EXTER)");
    aic_wr(Timer, ST_CS, ST_M1);
}

/* 7. The storm guard */
static void Storm(void)
{
    ULONG start, took;
    ULONG count0 = Query(LINE, AICI_Count), unclaimed0 = Query(LINE, AICI_Unclaimed);

    Print("\n[7] STORM: a server that neither claims nor clears the device\n");

    InitServer(&int_s, &srv_s, STORM, 0, "aic-test S");
    aic_wr(Timer, ST_CS, ST_M1);
    CheckValue(AIC_AddIntServer(LINE, &int_s), AICERR_OK, "AIC_AddIntServer()");

    Fire(300);
    start = Now();
    while (!Query(LINE, AICI_Stormed) && Now() - start < 2000000)
        ;
    took = Now() - start;

    CheckValue(Query(LINE, AICI_Stormed), TRUE, "AICI_Stormed");
    CheckValue(Query(LINE, AICI_Enabled), FALSE, "AICI_Enabled");
    Print("  disabled after about %ld us; calls of the server %ld, AICI_Count +%ld, AICI_Unclaimed +%ld\n",
        took, srv_s.calls, Query(LINE, AICI_Count) - count0, Query(LINE, AICI_Unclaimed) - unclaimed0);
    Check(Query(LINE, AICI_Unclaimed) - unclaimed0 >= 1000, "AICI_Unclaimed reached the limit (1000)");

    aic_wr(Timer, ST_CS, ST_M1);
    CheckValue(AIC_EnableInt(LINE), AICERR_OK, "the device cleared, AIC_EnableInt()");
    CheckValue(Query(LINE, AICI_Stormed), FALSE, "AICI_Stormed");
    CheckValue(Query(LINE, AICI_Enabled), TRUE, "AICI_Enabled");
    CheckValue(AIC_RemIntServer(LINE, &int_s), AICERR_OK, "removed");
}

/* The vertical blank of the HDMI output, through the library */
static void VBlank(void)
{
    APTR node = FindCompatible("brcm,bcm2835-pixelvalve2");
    ULONG pv, control, inten, start, took, rate, count0, unclaimed0;
    LONG irq;

    Print("\n[9] VBLANK: pixelvalve 2 (HDMI) through the library\n");

    if (node == NULL || (pv = SocAddress(node)) == 0)
    {
        Print("  no brcm,bcm2835-pixelvalve2 in the device tree: skipped\n");
        return;
    }

    irq = AIC_GetInterrupt(node, 0);
    CheckValue(irq, 74, "AIC_GetInterrupt(pixelvalve2, 0)");
    if (irq < 0)
        return;

    control = aic_rd(pv, PV_CONTROL);
    inten = aic_rd(pv, PV_INTEN);
    Print("  pixelvalve 2 at %08lx: CONTROL %08lx INTEN %08lx INTSTAT %08lx\n", pv, control, inten, aic_rd(pv, PV_INTSTAT));

    if ((control & PV_CONTROL_EN) == 0)
    {
        Print("  pixelvalve 2 does not run (no HDMI picture): skipped\n");
        return;
    }
    if (inten & PV_INT_VFP_START)
        Print("  note: PV_INT_VFP_START was already enabled by someone (the firmware?)\n");

    InitServer(&int_v, &srv_v, VBLANK, 0, "aic-test V");
    srv_v.device = pv;
    srv_v.min = 0xffffffff;
    srv_v.max = 0;
    count0 = Query(irq, AICI_Count);
    unclaimed0 = Query(irq, AICI_Unclaimed);
    CheckValue(AIC_AddIntServer(irq, &int_v), AICERR_OK, "AIC_AddIntServer(74)");

    aic_wr(pv, PV_INTSTAT, PV_INT_VFP_START);
    aic_wr(pv, PV_INTEN, inten | PV_INT_VFP_START);
    start = Now();
    Delay(100);
    took = Now() - start;
    aic_wr(pv, PV_INTEN, inten);
    Wait_us(50000);

    rate = took >= 1000 ? srv_v.hits * 1000000 / (took / 1000) : 0;
    Print("  %ld vertical blanks in %ld us: %ld.%03ld Hz; between two: min %ld us, max %ld us\n", srv_v.hits, took,
        rate / 1000, rate % 1000, srv_v.hits > 1 ? srv_v.min : 0, srv_v.max);
    Print("  server calls %ld, AICI_Count +%ld, AICI_Unclaimed +%ld\n", srv_v.calls, Query(irq, AICI_Count) - count0,
        Query(irq, AICI_Unclaimed) - unclaimed0);
    Check(rate >= 23000 && rate <= 121000, "one interrupt a frame (23 to 121 Hz)");
    CheckValue(Query(irq, AICI_Stormed), FALSE, "AICI_Stormed");
    if (Query(irq, AICI_Unclaimed) != unclaimed0)
        Print("  note: some rings found PV_INTSTAT already cleared: the firmware may take this interrupt too\n");

    CheckValue(AIC_RemIntServer(irq, &int_v), AICERR_OK, "removed");
    Print("  pixelvalve 2 after: INTEN %08lx INTSTAT %08lx\n", aic_rd(pv, PV_INTEN), aic_rd(pv, PV_INTSTAT));
}

int main(int wb, struct WBStartup *msg)
{
    LONG args[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    ULONG others[ALL_LINES], other_counts[ALL_LINES], irq;
    struct RDArgs *rd;
    static char path[256];
    APTR timer_node, mbox;
    ULONG controller, mem_before;
    APTR coldreboot;
    ULONG enables[3], stale;
    LONG count = 20;
    struct TagItem bad[2];

    SysBase = *(struct ExecBase **)4UL;
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37);
    if (DOSBase == NULL || wb)
        return RETURN_FAIL;

    rd = ReadArgs((CONST_STRPTR)"LIB/A,N/N,STORM/S,VBLANK/S,REBOOT/S,CONTROL/S,LINEOFF/S,SHARED/S", args, NULL);
    if (rd == NULL)
    {
        PrintFault(IoErr(), (CONST_STRPTR)"aic-test");
        CloseLibrary((struct Library *)DOSBase);
        return RETURN_FAIL;
    }
    if (args[1])
        count = *(LONG *)args[1];
    if (count < 1 || count > 10000)
        count = 20;

    Print("aic-test 0.5: arm-intc.library through its calls\n");

    DeviceTreeBase = OpenResource((CONST_STRPTR)"devicetree.resource");
    timer_node = DeviceTreeBase ? FindCompatible("brcm,bcm2835-system-timer") : NULL;
    mbox = DeviceTreeBase ? FindCompatible("brcm,bcm2835-mbox") : NULL;
    Timer = timer_node ? SocAddress(timer_node) : 0;
    if (Timer == 0)
    {
        Print("no system timer in the device tree\n");
        goto out;
    }

    /* CONTROL and LINEOFF: alone */
    if (args[6])
    {
        Print("\n[LINEOFF] ENABLE1 %08lx", aic_rd(0xf200b200, ARMCTRL_ENABLE1));
        aic_wr(0xf200b200, ARMCTRL_DISABLE1, 1UL << (LINE & 31));
        Print(", line %ld disabled by hand: ENABLE1 %08lx\n", (LONG)LINE, aic_rd(0xf200b200, ARMCTRL_ENABLE1));
        goto out;
    }
    if (args[5])
    {
        Print("\n[CONTROL] line %ld enabled by hand, no library, then ColdReboot(): after the restart, aic-probe must show bit 1 of "
            "ENABLE1\n", (LONG)LINE);
        if (FindLibrary() != NULL)
        {
            Print("  [FAIL] arm-intc.library is in memory: its reset guard would disable the line\n");
            Failures++;
            goto out;
        }
        /* The next match more than an hour away; LINEOFF after the restart */
        aic_wr(Timer, ST_CS, ST_M1);
        aic_wr(Timer, ST_C1, Now() - 1);
        aic_wr(0xf200b200, ARMCTRL_ENABLE1, 1UL << (LINE & 31));
        Print("  ENABLE1 %08lx; ColdReboot() in 3 seconds\n", aic_rd(0xf200b200, ARMCTRL_ENABLE1));
        Delay(150);
        ColdReboot();
    }

    /* 1. Open */
    if (args[7])
    {
        Print("\n[1] SHARED: open the library already in memory\n");
        Check(FindLibrary() != NULL, "in memory before (opened by another driver)");
    }
    else
    {
        Print("\n[1] open %s\n", (LONG)args[0]);
        Check(FindLibrary() == NULL, "not in memory before (a clean start)");
    }
    coldreboot = ColdRebootVector();
    enables[0] = aic_rd(0xf200b200, ARMCTRL_ENABLE0);
    enables[1] = aic_rd(0xf200b200, ARMCTRL_ENABLE1);
    enables[2] = aic_rd(0xf200b200, ARMCTRL_ENABLE2);
    mem_before = AvailMem(MEMF_ANY);
    ArmIntcBase = args[7] ? OpenLibrary((CONST_STRPTR)ARMINTCNAME, 0) : OpenFromFile((const char *)args[0], path, sizeof(path));
    if (ArmIntcBase == NULL)
    {
        Print("  [FAIL] cannot open %s\n", (LONG)args[0]);
        Failures++;
        goto out;
    }
    Print("  %s\n", (LONG)ArmIntcBase->lib_IdString);
    CheckValue(Query(AIC_CONTROLLER, AICI_Version), (AIC_VERSION_EXPECTED << 16) | AIC_REVISION_EXPECTED, "AICI_Version");
    controller = Query(AIC_CONTROLLER, AICI_ControllerAddress);
    Print("  controller %08lx, local %08lx, GPU routing %08lx, stale lines %ld\n", controller,
        Query(AIC_CONTROLLER, AICI_LocalAddress), Query(AIC_CONTROLLER, AICI_GpuRouting), Query(AIC_CONTROLLER, AICI_StaleLines));
    Check(controller == 0xf200b200, "the controller at F200B200 (as aic-probe found it)");
    Check(aic_rd(controller, ARMCTRL_ENABLE0) == enables[0] && aic_rd(controller, ARMCTRL_ENABLE1) == enables[1] &&
        aic_rd(controller, ARMCTRL_ENABLE2) == enables[2], "the open changed no enable register");
    stale = Query(AIC_CONTROLLER, AICI_StaleLines);
    if (args[7])
    {
        /* The lines of the other drivers, to compare at the end */
        for (irq = 0; irq < ALL_LINES; irq++)
        {
            others[irq] = Query(irq, AICI_Servers);
            if (others[irq] == 0xdeadbeef)
                others[irq] = 0;
            other_counts[irq] = others[irq] ? Query(irq, AICI_Count) : 0;
            BaseServers += others[irq];
            if (others[irq])
                Print("  line %ld: %ld servers of another driver, count %ld, enabled %ld\n", (LONG)irq, others[irq],
                    other_counts[irq], Query(irq, AICI_Enabled));
        }
        Check(BaseServers != 0 && others[LINE] == 0, "another driver has servers, none on line 33");
        CheckValue(Query(AIC_CONTROLLER, AICI_ServerCount), BaseServers, "AICI_ServerCount, the servers of the other drivers");
        CheckValue(Query(AIC_CONTROLLER, AICI_ResetGuard), TRUE, "AICI_ResetGuard, installed by their first server");
    }
    else
    {
        CheckValue(Query(AIC_CONTROLLER, AICI_ServerCount), 0, "AICI_ServerCount");
        CheckValue(Query(AIC_CONTROLLER, AICI_ResetGuard), FALSE, "AICI_ResetGuard before the first server");
    }
    Check(ColdRebootVector() == coldreboot, "ColdReboot() not patched by the open");

    /* 2. Bad calls */
    Print("\n[2] bad calls\n");
    InitServer(&int_a, &srv_a, CLAIM, 0, "aic-test A");
    CheckValue(AIC_AddIntServer(LINE, NULL), AICERR_BADARGS, "AIC_AddIntServer(33, NULL)");
    int_a.is_Code = NULL;
    CheckValue(AIC_AddIntServer(LINE, &int_a), AICERR_BADARGS, "AIC_AddIntServer() with is_Code NULL");
    int_a.is_Code = (VOID (*)())TestServer;
    CheckValue(AIC_AddIntServer(8, &int_a), AICERR_BADIRQ, "AIC_AddIntServer(8)");
    CheckValue(AIC_AddIntServer(96, &int_a), AICERR_BADIRQ, "AIC_AddIntServer(96)");
    CheckValue(AIC_RemIntServer(LINE, &int_a), AICERR_NOTFOUND, "AIC_RemIntServer() of a server not added");
    CheckValue(AIC_RemIntServer(LINE, NULL), AICERR_BADARGS, "AIC_RemIntServer(33, NULL)");
    CheckValue(AIC_EnableInt(LINE), AICERR_NOSERVER, "AIC_EnableInt() with no server");
    CheckValue(AIC_DisableInt(LINE), AICERR_NOSERVER, "AIC_DisableInt() with no server");
    CheckValue(AIC_EnableInt(31), AICERR_BADIRQ, "AIC_EnableInt(31)");
    CheckValue(AIC_GetInterrupt(NULL, 0), AICERR_BADARGS, "AIC_GetInterrupt(NULL)");
    CheckValue(AIC_GetInterrupt(timer_node, 1), LINE, "AIC_GetInterrupt(timer, 1)");
    CheckValue(AIC_GetInterrupt(timer_node, 4), AICERR_NODT, "AIC_GetInterrupt(timer, 4)");
    CheckValue(AIC_GetInterrupt(mbox, 0), AIC_IRQ_ARM_MAILBOX, "AIC_GetInterrupt(mailbox, 0)");
    bad[0].ti_Tag = AICI_Count;
    bad[0].ti_Data = (ULONG)&controller;
    bad[1].ti_Tag = TAG_DONE;
    CheckValue(AIC_QueryInfoTagList(8, bad), 0, "AIC_QueryInfoTagList(8)");
    CheckValue(Query(LINE, AIC_Dummy + 99), 0xdeadbeef, "an unknown tag leaves its answer alone");

    /* 3 to 6 */
    OneServer(count);
    Chain(count);
    DisableEnable();
    LastOut();

    /* 7 */
    if (args[2])
        Storm();

    if (args[3])
        VBlank();

    /* SHARED: the lines of the other drivers kept ringing and were not touched; no expunge, no reboot */
    if (args[7])
    {
        Print("\n[8] SHARED: the lines of the other drivers\n");
        for (irq = 0; irq < ALL_LINES; irq++)
        {
            ULONG now;

            if (others[irq] == 0)
                continue;
            now = Query(irq, AICI_Count);
            Print("  line %ld: count %ld -> %ld, unclaimed %ld\n", (LONG)irq, other_counts[irq], now, Query(irq, AICI_Unclaimed));
            CheckValue(Query(irq, AICI_Servers), others[irq], "its servers");
            CheckValue(Query(irq, AICI_Enabled), TRUE, "enabled");
            CheckValue(Query(irq, AICI_Stormed), FALSE, "not stormed");
            Check(now != other_counts[irq], "it kept ringing (count went up)");
        }
        CheckValue(Query(AIC_CONTROLLER, AICI_ServerCount), BaseServers, "AICI_ServerCount back to theirs");
        Check(ColdRebootVector() == coldreboot, "ColdReboot() as before");
        if (args[4])
            Print("  REBOOT skipped: not with another driver's servers\n");
        goto out;
    }

    /* 8. Expunge, then a new start */
    Print("\n[8] expunge, and a line enabled by another driver at the next start\n");
    CloseLibrary(ArmIntcBase);
    ArmIntcBase = NULL;
    FlushMemory();
    Check(FindLibrary() == NULL, "the library left the memory (expunged)");
    Check(ColdRebootVector() == coldreboot, "the expunge gave ColdReboot() back");
    Print("  free memory before opening %ld, now %ld (others may allocate meanwhile)\n", mem_before, AvailMem(MEMF_ANY));

    /* As another driver would (or a warm reboot without the guard): the line enabled. The next match is more than an hour away. */
    aic_wr(Timer, ST_CS, ST_M1);
    aic_wr(Timer, ST_C1, Now() - 1);
    aic_wr(0xf200b200, ARMCTRL_ENABLE1, 1UL << (LINE & 31));
    Print("  line %ld enabled by hand: ENABLE1 %08lx\n", (LONG)LINE, aic_rd(0xf200b200, ARMCTRL_ENABLE1));

    ArmIntcBase = OpenFromFile((const char *)args[0], path, sizeof(path));
    if (ArmIntcBase == NULL)
    {
        aic_wr(0xf200b200, ARMCTRL_DISABLE1, 1UL << (LINE & 31));
        Print("  [FAIL] cannot open it again\n");
        Failures++;
        goto out;
    }
    CheckValue(Query(AIC_CONTROLLER, AICI_StaleLines), stale + 1, "AICI_StaleLines, one more than at the first open");
    Check((aic_rd(0xf200b200, ARMCTRL_ENABLE1) & (1UL << (LINE & 31))) != 0, "the library left it enabled");
    aic_wr(0xf200b200, ARMCTRL_DISABLE1, 1UL << (LINE & 31));
    CloseLibrary(ArmIntcBase);
    ArmIntcBase = NULL;
    /* Out of memory again: the next run must find a clean start */
    FlushMemory();
    Check(FindLibrary() == NULL, "the library left the memory again (for the next run)");

    /* REBOOT: the reset guard on a real ColdReboot(), only after everything else passed */
    if (args[4] && Failures == 0)
    {
        Print("\n[REBOOT] a server on line %ld, then ColdReboot(): after the restart, aic-probe must show ENABLE1 without bit 1\n",
            (LONG)LINE);
        ArmIntcBase = OpenFromFile((const char *)args[0], path, sizeof(path));
        if (ArmIntcBase == NULL)
        {
            Print("  [FAIL] cannot open it again\n");
            Failures++;
            goto out;
        }
        InitServer(&int_a, &srv_a, CLAIM, 0, "aic-test A");
        aic_wr(Timer, ST_CS, ST_M1);
        aic_wr(Timer, ST_C1, Now() - 1);
        CheckValue(AIC_AddIntServer(LINE, &int_a), AICERR_OK, "AIC_AddIntServer()");
        CheckValue(Query(AIC_CONTROLLER, AICI_ResetGuard), TRUE, "AICI_ResetGuard");
        Print("  ENABLE1 %08lx; ColdReboot() in 3 seconds\n", aic_rd(0xf200b200, ARMCTRL_ENABLE1));
        Delay(150);
        ColdReboot();
    }

out:
    Print("\n%ld checks, %ld failed\n", (LONG)Checks, (LONG)Failures);

    if (ArmIntcBase)
        CloseLibrary(ArmIntcBase);
    FreeArgs(rd);
    CloseLibrary((struct Library *)DOSBase);

    return Failures ? RETURN_WARN : RETURN_OK;
}
