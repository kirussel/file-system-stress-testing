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

#define NDEBUG
#include "fembot.h"
#include "tap.h"

#include <signal.h>
#include <string.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "bstg.h"

__RCSID("$Id$");

#define NCHILD  4
#define NOPS    1000
#define NFUNCS  3

static const char *const names[NFUNCS] = { "a/one", "b/two", "c/three" };

int
main()
{
    bstg_status_t *st;
    pid_t pid[NCHILD + 1];
    unsigned i, x;
    u_int64_t sum;
    int status;
    FILE *out;
    char buf[8192];

    plan_tests(11);

    st = bstg_status_init(NFUNCS, NCHILD + 1);
    ok(st != NULL, "init");
    if (st == NULL) {
        return 1;
    }

    for (i = 0; i < NCHILD; i++) {
        pid[i] = fork();
        if (pid[i] == 0) {
            for (x = 0; x < NOPS; x++) {
                bstg_status_op(st, i, x % NFUNCS);
            }
            _exit(0);
        }
        bstg_status_child(st, i, pid[i]);
    }

    /* one more worker that dies inside op 2 */
    pid[NCHILD] = fork();
    if (pid[NCHILD] == 0) {
        bstg_status_op(st, NCHILD, 2);
        raise(SIGKILL);
        _exit(0);
    }
    bstg_status_child(st, NCHILD, pid[NCHILD]);

    for (i = 0; i < NCHILD + 1; i++) {
        pid_t p = wait(&status);

        bstg_status_reaped(st, p, status);
    }

    ok(st->total == NCHILD * NOPS + 1, "total");
    sum = 0;
    for (i = 0; i < NFUNCS; i++) {
        sum += st->opcount[i];
    }
    ok(sum == st->total, "per op counts sum to total");
    ok(st->opcount[2] >= NCHILD * (NOPS / NFUNCS) + 1, "op 2 counted");
    ok(st->deaths == NCHILD + 1, "deaths");
    ok(st->lastdeath >= 0, "lastdeath set");
    ok(st->child[NCHILD].dead && st->child[NCHILD].lastop == 2 &&
        WIFSIGNALED(st->child[NCHILD].wstatus) &&
        WTERMSIG(st->child[NCHILD].wstatus) == SIGKILL,
        "killed worker's op and signal recorded");

    /* reap of an unknown pid is ignored */
    bstg_status_reaped(st, 1, 0);
    ok(st->deaths == NCHILD + 1, "unknown pid ignored");

    memset(buf, 0, sizeof buf);
    out = fmemopen(buf, sizeof buf, "w");
    if (out != NULL) {
        bstg_status_draw(st, out, names, "test");
        fclose(out);
    }
    ok(strstr(buf, "a/one") != NULL && strstr(buf, "deaths") != NULL,
        "draw");

    /* a failed fork() is not a worker */
    bstg_status_child(st, 0, -1);
    ok(st->child[0].pid == pid[0], "bad pid ignored");

    /* without -s the callers pass a NULL status */
    bstg_status_child(NULL, 0, 1);
    bstg_status_op(NULL, 0, 0);
    bstg_status_reaped(NULL, 1, 0);
    bstg_status_draw(NULL, stdout, names, "test");
    ok(bstg_status_destroy(NULL) == 0, "NULL status is a no-op");

    bstg_status_destroy(st);
    return 0;
}
