/*
    Copyright © 2025 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <exec/types.h>
#include <utility/tagitem.h>

#include "base.h"

/* utility.library/NextTagItem(), without opening utility.library: the next tag of the list that is not a control tag, NULL at the end */
const struct TagItem *AIC_NextTag(
    const struct TagItem **list)
{
    const struct TagItem *t = *list;

    while (t != NULL)
    {
        switch (t->ti_Tag)
        {
            case TAG_DONE:
                t = NULL;
                break;

            case TAG_IGNORE:
                t++;
                break;

            case TAG_MORE:
                t = (const struct TagItem *)t->ti_Data;
                break;

            case TAG_SKIP:
                t += t->ti_Data + 1;
                break;

            default:
                *list = t + 1;
                return t;
        }
    }

    *list = NULL;

    return NULL;
}
