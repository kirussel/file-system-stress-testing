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
#include <time.h>

#include "fembot.h"
#include "bstg.h"

__RCSID("$Id$");

#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif

/* a worker is stalled when its current op has run this many seconds */
#define STALL_SECS  10
#define TOP_OPS     15

#define LOAD(__p)       __atomic_load_n((__p), __ATOMIC_RELAXED)
#define STORE(__p, __v) __atomic_store_n((__p), (__v), __ATOMIC_RELAXED)

/*
 * Seconds from a clock that does not jump when the wall clock is set,
 * so stall times and rates stay sane.
 */
static time_t
now_secs(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

/* opcount rows are padded to whole cache lines */
static unsigned
rowstride(unsigned nfuncs)
{
    unsigned per = BSTG_STATUS_ALIGN / sizeof(u_int64_t);

    return (nfuncs + per - 1) / per * per;
}

static size_t
maplen(unsigned nfuncs, unsigned nchildren)
{
    return sizeof(bstg_status_t) + nchildren * sizeof(bstg_status_child_t) +
        (size_t)nchildren * rowstride(nfuncs) * sizeof(u_int64_t);
}

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

    len = maplen(nfuncs, nchildren);
    st = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);
    if (st == MAP_FAILED) {
        return NULL;
    }

    /* the mapping is zero filled */
    st->magic = BSTG_STATUS_MAGIC;
    st->nfuncs = nfuncs;
    st->nchildren = nchildren;
    st->stride = rowstride(nfuncs);
    st->start = now_secs();
    st->child = (bstg_status_child_t *)(st + 1);
    st->opcount = (u_int64_t *)(st->child + nchildren);
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
    u_int64_t *p;

    if (st == NULL || st->magic != BSTG_STATUS_MAGIC ||
            idx >= st->nchildren || f >= st->nfuncs) {
        return;
    }
    /* only this worker writes its counters: no atomic read-modify-write */
    p = &st->opcount[(size_t)idx * st->stride + f];
    STORE(&st->child[idx].lastop_at, now_secs());
    STORE(&st->child[idx].lastop, (int)f);
    STORE(p, LOAD(p) + 1);
}

u_int64_t
bstg_status_opcount(bstg_status_t *st, unsigned f)
{
    u_int64_t n;
    unsigned i;

    if (st == NULL || st->magic != BSTG_STATUS_MAGIC || f >= st->nfuncs) {
        return 0;
    }
    n = 0;
    for (i = 0; i < st->nchildren; i++) {
        n += LOAD(&st->opcount[(size_t)i * st->stride + f]);
    }
    return n;
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
            st->lastdeath_at = now_secs();
            return;
        }
    }
}

/* lastop is written by a worker, so do not trust it as an index */
static const char *
opname(bstg_status_t *st, const char *const names[], int op)
{
    if (op < 0) {
        return "(none)";
    }
    return (unsigned)op < st->nfuncs ? names[op] : "(unknown)";
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
    unsigned i, alive, nstalled, *order;
    FILE *real;
    u_int64_t total, *snap;
    time_t now;
    char *buf, *p;
    size_t buflen;
    FILE *mem;
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
        snap[i] = bstg_status_opcount(st, i);
        total += snap[i];
        order[i] = i;
    }

    now = now_secs();
    avg = now > st->start ? (double)total / (now - st->start) : 0;
    cur = st->prevtime && now > st->prevtime ?
        (double)(total - st->prevtotal) / (now - st->prevtime) : avg;
    st->prevtotal = total;
    st->prevtime = now;

    alive = 0;
    for (i = 0; i < st->nchildren; i++) {
        if (st->child[i].pid && !st->child[i].dead) {
            alive++;
        }
    }

    /*
     * Render the frame to memory first, then repaint in place: home the
     * cursor, erase each line's old tail as it is rewritten and erase
     * whatever is left below. Clearing the screen first would flicker.
     */
    buf = NULL;
    mem = open_memstream(&buf, &buflen);
    if (mem == NULL) {
        free(order);
        free(snap);
        return;
    }
    real = out;
    out = mem;
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
        fprintf(out, " in %s (%ld secs ago)", opname(st, names, c->lastop),
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
                    opname(st, names, op), (long)(now - at));
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

    fclose(mem);
    fputs("\033[H", real);
    for (p = buf; *p != '\0'; p++) {
        if (*p == '\n') {
            fputs("\033[K", real);
        }
        fputc(*p, real);
    }
    fputs("\033[J", real);
    fflush(real);
    free(buf);
}

int
bstg_status_destroy(bstg_status_t *st)
{
    size_t len;

    if (st == NULL || st->magic != BSTG_STATUS_MAGIC) {
        return 0;
    }
    len = maplen(st->nfuncs, st->nchildren);
    st->magic = 0;
    return munmap(st, len);
}
