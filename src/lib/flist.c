/*
 * Copyright 2011 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fembot.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>

#include "bstg.h"

__RCSID("$Id$");


int
bstg_flist_init(bstg_flist_t *ps, u_int32_t number)
{
    u_int32_t x;

    if ((ps->pindex = calloc(number, sizeof(*ps->pindex)))) {
        ps->upper = ps->number = ps->capacity = number;
        ps->magic = BSTG_FLIST_MAGIC;
        for (x = 0; x < number; x++) {
            ps->pindex[x] = x;
        }
        return ps->lower = 0;
    }
    return 1;
}

int
bstg_flist_destroy(bstg_flist_t *ps)
{
    assert(ps->magic == BSTG_FLIST_MAGIC);
    assert(ps->pindex != NULL);
    free(ps->pindex);
    ps->pindex = NULL;
    ps->magic = ~BSTG_FLIST_MAGIC;

    return 0;
}

int
bstg_flist_shuffle(bstg_flist_t *ps)
{
    u_int32_t len, swapindex;
    u_int32_t temp;

    assert(ps->magic == BSTG_FLIST_MAGIC);
    len = ps->number;
    while (len > 1) {
        swapindex = arc4random_uniform(len--);
        temp = ps->pindex[len];
        ps->pindex[len] = ps->pindex[swapindex];
        ps->pindex[swapindex] = temp;
    }

    return 0;
}

int
bstg_flist_set(bstg_flist_t *ps, u_int32_t lower, u_int32_t upper)
{
    if (lower >= upper) {
        return 1;
    }
    if (upper > ps->number) {
        return 1;
    }
    ps->lower = lower;
    ps->upper = upper;

    return 0;
}

int
bstg_flist_get(bstg_flist_t *ps, u_int32_t index)
{
    u_int32_t range;

    range = ps->upper - ps->lower;
    return ps->pindex[(index % range) + ps->lower];
}

/*
 * Parse options as a list of numbers separated by " ,:". Store them in
 * pindex if store is set, and set *pcount to how many were found. Return
 * 1 if anything else is in the string, there are more than capacity, or
 * one doesn't fit in a u_int32_t.
 */
static int
flist_parse(bstg_flist_t *ps, const char *options, int store, u_int32_t *pcount)
{
    const char *curr;
    char *p;
    unsigned long number;
    u_int32_t count;

    count = 0;
    curr = options;
    for (;;) {
        /* skip separators */
        curr += strspn(curr, " ,:");
        if (*curr == '\0') {
            break;
        }

        /* strtoul() would also take white space, a sign or a 0x prefix */
        if (!isdigit((unsigned char)*curr)) {
            return 1;
        }
        errno = 0;
        number = strtoul(curr, &p, 10);
        if (errno == ERANGE || number > UINT32_MAX) {
            return 1;
        }
        if (count >= ps->capacity) {
            return 1;
        }
        if (store) {
            ps->pindex[count] = (u_int32_t)number;
        }
        count++;
        curr = p;
    }

    *pcount = count;
    return 0;
}

/*
 * Replace the list with the numbers in options. The list may shrink and
 * grow again, up to the capacity it was created with. On failure (no
 * numbers, anything but numbers and separators, more numbers than the
 * capacity, or a number too big) the list is left unchanged.
 */
int
bstg_flist_import(bstg_flist_t *ps, char *options)
{
    u_int32_t count;

    assert(ps->magic == BSTG_FLIST_MAGIC);
    if (flist_parse(ps, options, 0, &count) || count == 0) {
        return 1;
    }
    (void)flist_parse(ps, options, 1, &count);
    ps->lower = 0;
    ps->upper = ps->number = count;

    return 0;
}
