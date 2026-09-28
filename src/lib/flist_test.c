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

#define NDEBUG
#include "fembot.h"
#include "tap.h"

#include <errno.h>
#include <sys/cdefs.h>

#include "bstg.h"

__RCSID("$Id$");

#define FLIST_SIZE 10

/* count entries that are no longer in their original (identity) position */
static int
count_moved(bstg_flist_t *ps)
{
    u_int32_t x;
    int count = 0;

    for (x = 0; x < FLIST_SIZE; x++) {
        if (ps->pindex[x] != x) {
            count++;
        }
    }
    return count;
}

/* true if pindex holds each of 0..FLIST_SIZE-1 exactly once */
static int
is_permutation(bstg_flist_t *ps)
{
    u_int32_t x;
    int seen[FLIST_SIZE] = { 0 };

    for (x = 0; x < FLIST_SIZE; x++) {
        if (ps->pindex[x] >= FLIST_SIZE || seen[ps->pindex[x]]++) {
            return 0;
        }
    }
    return 1;
}

int
main()
{
    bstg_flist_t flist, control;

    plan_tests(61);

    ok(BSTG_FLIST_MAGIC != -1, "magic is not -1");
    ok(BSTG_FLIST_MAGIC != 0, "magic is not 0");

    ok(bstg_flist_init(&flist, FLIST_SIZE) == 0, "simple init");
    ok(flist.magic == BSTG_FLIST_MAGIC, "magic was set");
    ok(flist.number == 10, "verbose was set");
    ok(flist.pindex[0] == 0, "zero index");

    ok(bstg_flist_set(&flist, 6, 5) == 1, "range error");
    ok(bstg_flist_set(&flist, 1, 11) == 1, "another range error");

    ok(bstg_flist_get(&flist, 0) == 0, "zero");
    ok(bstg_flist_get(&flist, 1) == 1, "one");
    ok(bstg_flist_get(&flist, 5) == 5, "five");
    ok(bstg_flist_get(&flist, 99) == 9, "nine");
    ok(bstg_flist_set(&flist, 0, 10) == 0, "nop");
    ok(bstg_flist_get(&flist, 1) == 1, "one");
    ok(bstg_flist_get(&flist, 5) == 5, "five");
    ok(bstg_flist_get(&flist, 99) == 9, "nine");

    ok(bstg_flist_set(&flist, 1, 10) == 0, "nop");
    ok(bstg_flist_get(&flist, 0) == 1, "zero moved");
    ok(bstg_flist_get(&flist, 1) == 2, "one moved");
    ok(bstg_flist_get(&flist, 89) == 9, "now nine");
    ok(bstg_flist_set(&flist, 1, 9) == 0, "nop");
    ok(bstg_flist_get(&flist, 0) == 1, "zero moved");
    ok(bstg_flist_get(&flist, 1) == 2, "one moved");
    ok(bstg_flist_get(&flist, 87) == 8, "now eight");

    /* the shuffle check must not pass on a list that was never shuffled */
    ok(bstg_flist_init(&control, FLIST_SIZE) == 0, "control init");
    ok(count_moved(&control) == 0, "unshuffled list reports nothing moved");
    ok(bstg_flist_destroy(&control) == 0, "control destroy");

    ok(bstg_flist_set(&flist, 0, 10) == 0, "nop");
    ok(bstg_flist_shuffle(&flist) == 0, "shuffle");
    ok(is_permutation(&flist), "shuffle kept a permutation");
    /*
     * A random permutation of 10 has <= 1 fixed point only ~74% of the
     * time, so "> 8" would be flaky; only the identity (1/10!) fails "> 0".
     */
    ok(count_moved(&flist) > 0, "shuffled");

    /* a failed import must leave the shuffled list alone */
    ok(bstg_flist_import(&flist, "0,1,2,3,4,5,6,7,8,9,10") == 1,
        "over capacity import");
    ok(bstg_flist_import(&flist, "abc") == 1, "bad import");
    ok(bstg_flist_import(&flist, "") == 1, "empty import");
    ok(bstg_flist_import(&flist, " ,: ") == 1, "separators only import");
    ok(bstg_flist_import(&flist, "-1") == 1, "negative import");
    ok(bstg_flist_import(&flist, "4294967296") == 1, "too big import");
    ok(flist.number == FLIST_SIZE, "failed imports kept number");
    ok(flist.upper == FLIST_SIZE, "failed imports kept upper");
    ok(is_permutation(&flist), "failed imports kept the list");
    ok((u_int32_t)bstg_flist_get(&flist, 0) == flist.pindex[0],
        "get after failure");

    ok(bstg_flist_import(&flist, "9:8:7:6:5:4:3:2:1:0") == 0,
        "full capacity import");
    ok(flist.number == FLIST_SIZE, "full import number");
    ok(flist.pindex[0] == 9 && flist.pindex[9] == 0, "full import values");

    ok(bstg_flist_import(&flist, "1, 12,33 ") == 0, "import");
    ok(flist.pindex[0] == 1, "1");
    ok(flist.pindex[1] == 12, "12");
    ok(flist.pindex[2] == 33, "33");
    ok(bstg_flist_get(&flist, 0) == 1, "1");
    ok(bstg_flist_get(&flist, 1) == 12, "12");
    ok(bstg_flist_get(&flist, 2) == 33, "33");
    ok(bstg_flist_get(&flist, 3) == 1, "now 1");
    ok(flist.number == 3, "import shrank the list");
    ok(flist.capacity == FLIST_SIZE, "import kept the capacity");
    ok(bstg_flist_import(&flist, "0,1,2,3,4,5,6,7,8,9,10") == 1,
        "over capacity import after shrinking");
    ok(flist.number == 3, "shrunk list kept its length");
    ok(bstg_flist_import(&flist, "4 5 6 7 8 9 10 11 12 13") == 0,
        "import can grow back to the capacity");
    ok(flist.number == FLIST_SIZE, "list grew back");
    ok(bstg_flist_get(&flist, 9) == 13, "grown list values");

    ok(bstg_flist_destroy(&flist) == 0, "simple destroy");
    ok(flist.magic != BSTG_FLIST_MAGIC, "magic was unset");

    return 0;
}
