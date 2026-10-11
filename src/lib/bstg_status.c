/*
 * Copyright 2026 The file-system-stress-testing Authors
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

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "fembot.h"
#include "bstg.h"

__RCSID("$Id$");

#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif

/* a worker is stalled when its current op has run this many seconds */
#define STALL_SECS  10
#define TOP_OPS     15

#define ADD(__p, __n)   __atomic_fetch_add((__p), (__n), __ATOMIC_RELAXED)
#define LOAD(__p)       __atomic_load_n((__p), __ATOMIC_RELAXED)
#define STORE(__p, __v) __atomic_store_n((__p), (__v), __ATOMIC_RELAXED)

/*
 * Allocate the counters in an anonymous shared mapping, so that workers
 * created by fork() update the same memory the parent draws from.
 *
 * Returns the status on success.
 * Returns NULL on error.
 */
bstg_status_t *
bstg_status_init(unsigned nfuncs, unsigned nchildren)
{
    bstg_status_t *st;
    size_t len;
    unsigned i;

    len = sizeof(*st) + nfuncs * sizeof(*st->opcount) +
        nchildren * sizeof(*st->child);
    st = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);
    if (st == MAP_FAILED) {
        return NULL;
    }

    /* the mapping is zero filled */
    st->magic = BSTG_STATUS_MAGIC;
    st->nfuncs = nfuncs;
    st->nchildren = nchildren;
    st->start = time(NULL);
    st->opcount = (u_int64_t *)(st + 1);
    st->child = (bstg_status_child_t *)(st->opcount + nfuncs);
    st->lastdeath = -1;
    for (i = 0; i < nchildren; i++) {
        st->child[i].lastop = -1;
    }
    return st;
}

void
bstg_status_child(bstg_status_t *st, unsigned idx, pid_t pid)
{
    if (st == NULL || st->magic != BSTG_STATUS_MAGIC ||
            idx >= st->nchildren || pid <= 0) {
        return;
    }
    st->child[idx].pid = pid;
}

/*
 * Called by a worker before it starts an op, so that if the op never
 * returns (or kills us) the parent still knows where we were.
 */
void
bstg_status_op(bstg_status_t *st, unsigned idx, unsigned f)
{
    if (st == NULL || st->magic != BSTG_STATUS_MAGIC ||
            idx >= st->nchildren || f >= st->nfuncs) {
        return;
    }
    STORE(&st->child[idx].lastop_at, time(NULL));
    STORE(&st->child[idx].lastop, (int)f);
    ADD(&st->opcount[f], 1);
    ADD(&st->total, 1);
}

void
bstg_status_reaped(bstg_status_t *st, pid_t pid, int wstatus)
{
    unsigned i;

    if (st == NULL || st->magic != BSTG_STATUS_MAGIC) {
        return;
    }
    for (i = 0; i < st->nchildren; i++) {
        if (st->child[i].pid == pid && !st->child[i].dead) {
            st->child[i].dead = 1;
            st->child[i].wstatus = wstatus;
            st->deaths++;
            st->lastdeath = (int)i;
            st->lastdeath_at = time(NULL);
            return;
        }
    }
}

static const char *
opname(const char *const names[], int op)
{
    return op < 0 ? "(none)" : names[op];
}

static void
elapsed(FILE *out, time_t secs)
{
    fprintf(out, "%ld days, %ld hrs, %ld min, %ld sec", (long)(secs / 86400),
        (long)(secs / 3600 % 24), (long)(secs / 60 % 60), (long)(secs % 60));
}

/* qsort_r() differs between BSD and glibc, so sort through a global */
static const u_int64_t *sortkey;

static int
bycount(const void *a, const void *b)
{
    u_int64_t x = sortkey[*(const unsigned *)a];
    u_int64_t y = sortkey[*(const unsigned *)b];

    return x < y ? 1 : x > y ? -1 : 0;
}

void
bstg_status_draw(bstg_status_t *st, FILE *out, const char *const names[],
    const char *tool)
{
    static u_int64_t prevtotal;
    static time_t prevtime;
    unsigned i, alive, nstalled, *order;
    u_int64_t total, *snap;
    time_t now;
    double avg, cur;

    if (st == NULL || st->magic != BSTG_STATUS_MAGIC) {
        return;
    }

    /*
     * Work from a snapshot: the workers keep counting while we sort and
     * print, and a comparator on changing values is not safe.
     */
    snap = malloc(st->nfuncs * sizeof(*snap));
    order = malloc(st->nfuncs * sizeof(*order));
    if (snap == NULL || order == NULL) {
        free(snap);
        free(order);
        return;
    }
    total = 0;
    for (i = 0; i < st->nfuncs; i++) {
        snap[i] = LOAD(&st->opcount[i]);
        total += snap[i];
        order[i] = i;
    }

    now = time(NULL);
    avg = now > st->start ? (double)total / (now - st->start) : 0;
    cur = prevtime && now > prevtime ?
        (double)(total - prevtotal) / (now - prevtime) : avg;
    prevtotal = total;
    prevtime = now;

    alive = 0;
    for (i = 0; i < st->nchildren; i++) {
        if (st->child[i].pid && !st->child[i].dead) {
            alive++;
        }
    }

    /* home the cursor and clear the screen */
    fputs("\033[H\033[2J", out);
    fprintf(out, " %s (fsst) ----------------------------------------\n",
        tool);
    fputs("  run time : ", out);
    elapsed(out, now - st->start);
    fprintf(out, "\n  total ops: %" PRIu64 "\n", total);
    fprintf(out, "  ops/sec  : %.0f (now)  %.0f (avg)\n", cur, avg);
    fprintf(out, "  workers  : %u/%u alive\n", alive, st->nchildren);
    fprintf(out, "  deaths   : %" PRIu64, st->deaths);
    if (st->lastdeath >= 0) {
        bstg_status_child_t *c = &st->child[st->lastdeath];

        fprintf(out, "   last: pid %ld ", (long)c->pid);
        if (WIFSIGNALED(c->wstatus)) {
            fprintf(out, "%s", strsignal(WTERMSIG(c->wstatus)));
        } else {
            fprintf(out, "exit %d", WEXITSTATUS(c->wstatus));
        }
        fprintf(out, " in %s (%ld secs ago)", opname(names, c->lastop),
            (long)(now - st->lastdeath_at));
    }
    fputc('\n', out);

    fprintf(out, " -- stalled workers (no progress > %d secs) ------------\n",
        STALL_SECS);
    nstalled = 0;
    for (i = 0; i < st->nchildren; i++) {
        bstg_status_child_t *c = &st->child[i];
        int op = LOAD(&c->lastop);
        time_t at = LOAD(&c->lastop_at);

        if (c->pid && !c->dead && op >= 0 && at != 0 &&
                now - at > STALL_SECS) {
            /* keep a hung box readable */
            if (nstalled++ < 10) {
                fprintf(out, "  pid %ld in %s for %ld secs\n", (long)c->pid,
                    opname(names, op), (long)(now - at));
            }
        }
    }
    if (nstalled > 10) {
        fprintf(out, "  ... and %u more\n", nstalled - 10);
    }

    fprintf(out, " -- op mix (top %d by count) --------------------------\n",
        TOP_OPS);
    sortkey = snap;
    qsort(order, st->nfuncs, sizeof(*order), bycount);
    for (i = 0; i < st->nfuncs && i < TOP_OPS; i++) {
        u_int64_t n = snap[order[i]];

        if (n == 0) {
            break;
        }
        fprintf(out, "  %-28s %12" PRIu64 " %5.1f%%\n",
            names[order[i]], n, total ? 100.0 * n / total : 0.0);
    }
    free(order);
    free(snap);
    fflush(out);
}

int
bstg_status_destroy(bstg_status_t *st)
{
    size_t len;

    if (st == NULL || st->magic != BSTG_STATUS_MAGIC) {
        return 0;
    }
    len = sizeof(*st) + st->nfuncs * sizeof(*st->opcount) +
        st->nchildren * sizeof(*st->child);
    st->magic = 0;
    return munmap(st, len);
}
