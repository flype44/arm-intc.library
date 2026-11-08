/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>
#include <proto/exec.h>

int main(int, struct WBStartup *);

/* Startup code with the Workbench message, the same pattern as the other tools of this ecosystem */
int __attribute__((used)) _start()
{
    struct ExecBase *SysBase = *(struct ExecBase **)4;
    struct Process *p = (struct Process *)SysBase->ThisTask;
    struct WBStartup *wbmsg = NULL;
    int ret;

    if (p->pr_CLI == 0)
    {
        WaitPort(&p->pr_MsgPort);
        wbmsg = (struct WBStartup *)GetMsg(&p->pr_MsgPort);
    }

    ret = main(wbmsg ? 1 : 0, wbmsg);

    if (wbmsg)
    {
        Forbid();
        ReplyMsg((struct Message *)wbmsg);
    }

    return ret;
}
