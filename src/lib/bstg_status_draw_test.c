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

/*
 * More dashboard tests (see bstg_status_test.c). The disk line is driven
 * through a pipe we own instead of the real statvfs helper, so the
 * figures are exact and nothing waits on the clock.
 */

#define NDEBUG
#include "fembot.h"
#include "tap.h"

#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "bstg.h"

__RCSID("$Id$");

#define NFUNCS  4

static const char *const names[NFUNCS] = {
    "a/one", "b/two", "c/three", "d/four"
};

static time_t
mono(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

static char *
draw(bstg_status_t *st, char *buf, size_t len)
{
    FILE *out;

    memset(buf, 0, len);
    out = fmemopen(buf, len, "w");
    if (out != NULL) {
        bstg_status_draw(st, out, names, "test");
        fclose(out);
    }
    return buf;
}

/* replace the real disk helper with a pipe; returns the write end */
static int
fakedisk(bstg_status_t *st)
{
    int fds[2];

    kill(st->disk.pid, SIGKILL);
    waitpid(st->disk.pid, NULL, 0);
    close(st->disk.fd);
    if (pipe(fds) != 0) {
        return -1;
    }
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    st->disk.fd = fds[0];
    st->disk.pid = -1;
    st->disk.len = 0;
    st->disk.have = 0;
    return fds[1];
}

static void
report(int wfd, time_t t, int err, unsigned long long size,
    unsigned long long used, unsigned long long avail,
    unsigned long long files, unsigned long long ffree)
{
    char line[256];
    int n;

    n = snprintf(line, sizeof line, "{\"t\":%lld,\"err\":%d,\"size\":%llu,"
        "\"used\":%llu,\"avail\":%llu,\"files\":%llu,\"ffree\":%llu}\n",
        (long long)t, err, size, used, avail, files, ffree);
    if (write(wfd, line, n) != n) {
        diag("write to fake disk helper failed");
    }
}

/* a fresh status whose disk helper is a pipe */
static bstg_status_t *
newst(unsigned nchildren, int *wfd)
{
    bstg_status_t *st = bstg_status_init(NFUNCS, nchildren);

    *wfd = st != NULL ? fakedisk(st) : -1;
    return st;
}

int
main()
{
    bstg_status_t *st;
    char buf[16384];
    char *p;
    unsigned i;
    pid_t pid, helper;
    int wfd, status;

    plan_tests(32);

    /* op mix: sorted by count, never-started ops omitted */
    st = newst(3, &wfd);
    if (st == NULL || wfd < 0) {
        return 1;
    }
    bstg_status_child(st, 0, 1000);
    for (i = 0; i < 5; i++) {
        bstg_status_op(st, 0, 2);
    }
    for (i = 0; i < 3; i++) {
        bstg_status_op(st, 0, 0);
    }
    bstg_status_op(st, 0, 1);
    draw(st, buf, sizeof buf);
    ok(strstr(buf, "total ops: 9") != NULL, "total ops");
    ok(strstr(buf, "workers  : 1/3 alive") != NULL, "workers alive");
    ok(strstr(buf, "55.6%") != NULL, "percentage of the total");
    ok(strstr(buf, "c/three") != NULL && strstr(buf, "a/one") != NULL &&
        strstr(buf, "b/two") != NULL &&
        strstr(buf, "c/three") < strstr(buf, "a/one") &&
        strstr(buf, "a/one") < strstr(buf, "b/two"), "sorted by count");
    ok(strstr(buf, "d/four") == NULL, "unused op omitted");

    /* out of range ops and workers are ignored */
    bstg_status_op(st, 0, NFUNCS);
    bstg_status_op(st, 3, 0);
    ok(bstg_status_opcount(st, 0) == 3 && bstg_status_opcount(st, NFUNCS) == 0,
        "out of range op or worker ignored");

    /* stalled workers */
    bstg_status_child(st, 1, 4242);
    bstg_status_child(st, 2, 4243);
    bstg_status_op(st, 1, 1);
    bstg_status_op(st, 2, 2);
    st->child[1].lastop_at = mono() - 60;
    draw(st, buf, sizeof buf);
    ok(strstr(buf, "pid 4242 in b/two for ") != NULL, "stalled worker listed");
    ok(strstr(buf, "pid 4243 in") == NULL, "busy worker not listed");
    close(wfd);
    bstg_status_destroy(st);

    /* the stalled list is capped at ten */
    st = bstg_status_init(NFUNCS, 14);
    for (i = 0; st != NULL && i < 14; i++) {
        bstg_status_child(st, i, 5000 + i);
        bstg_status_op(st, i, 0);
        st->child[i].lastop_at = mono() - 60;
    }
    draw(st, buf, sizeof buf);
    ok(strstr(buf, "pid 5009 in a/one") != NULL &&
        strstr(buf, "pid 5010 in a/one") == NULL, "ten stalled workers shown");
    ok(strstr(buf, "... and 4 more") != NULL, "rest counted");
    bstg_status_destroy(st);

    /* last death */
    st = newst(2, &wfd);
    pid = fork();
    if (pid == 0) {
        bstg_status_op(st, 0, 3);
        raise(SIGKILL);
        _exit(0);
    }
    bstg_status_child(st, 0, pid);
    pid = waitpid(pid, &status, 0);
    bstg_status_reaped(st, pid, status);
    draw(st, buf, sizeof buf);
    ok(strstr(buf, "deaths   : 1") != NULL, "death counted");
    ok(strstr(buf, "Killed") != NULL && strstr(buf, "in d/four") != NULL,
        "last death signal and op");
    ok(strstr(buf, "workers  : 0/2 alive") != NULL, "no workers alive");
    close(wfd);
    bstg_status_destroy(st);

    /* reaping a worker that exited rather than being killed */
    st = bstg_status_init(NFUNCS, 1);
    ok(bstg_status_done(st), "done when no worker was started");
    pid = fork();
    if (pid == 0) {
        _exit(7);
    }
    bstg_status_child(st, 0, pid);
    ok(!bstg_status_done(st), "not done while a worker runs");
    pid = waitpid(pid, &status, 0);
    bstg_status_reaped(st, pid, status);
    ok(st->child[0].dead && WIFEXITED(st->child[0].wstatus) &&
        WEXITSTATUS(st->child[0].wstatus) == 7, "exit status recorded");
    draw(st, buf, sizeof buf);
    ok(strstr(buf, "exit 7") != NULL, "exit status drawn");
    bstg_status_destroy(st);

    /* disk figures: 1G, 256M used, 768M avail, 1000 inodes of which 900 free */
    st = newst(1, &wfd);
    report(wfd, mono(), 0, 1ULL << 30, 256ULL << 20, 768ULL << 20, 1000, 900);
    draw(st, buf, sizeof buf);
    ok(strstr(buf, "size 1.0G") != NULL && strstr(buf, "used 256M") != NULL &&
        strstr(buf, "avail 768M") != NULL, "human sizes");
    ok(strstr(buf, "25%") != NULL && strstr(buf, "inodes 10%") != NULL,
        "capacity and inode percentages");
    ok(strstr(buf, "HUNG") == NULL, "fresh figures are not hung");

    /* newest report wins */
    report(wfd, mono(), 0, 1ULL << 30, 0, 1ULL << 30, 0, 0);
    report(wfd, mono(), 0, 2ULL << 30, 0, 2ULL << 30, 0, 0);
    draw(st, buf, sizeof buf);
    ok(strstr(buf, "size 2.0G") != NULL, "newest report wins");
    ok(strstr(buf, "inodes") == NULL, "no inodes line without inodes");

    /* percentages round up like df: 1000/3000 is 33.3% */
    report(wfd, mono(), 0, 3000, 1000, 2000, 0, 0);
    draw(st, buf, sizeof buf);
    ok(strstr(buf, "34%") != NULL, "percentage rounds up");

    /* garbage and half lines */
    report(wfd, mono(), 0, 1ULL << 30, 0, 1ULL << 30, 0, 0);
    if (write(wfd, "not json\n{\"t\":1", 15) != 15) {
        diag("write failed");
    }
    draw(st, buf, sizeof buf);
    ok(strstr(buf, "size 1.0G") != NULL, "garbage ignored");
    close(wfd);
    bstg_status_destroy(st);

    /* statvfs failed */
    st = newst(1, &wfd);
    report(wfd, mono(), 5, 0, 0, 0, 0, 0);
    draw(st, buf, sizeof buf);
    ok((p = strstr(buf, "unavailable: ")) != NULL && p[13] != '\n',
        "statvfs error shown");
    close(wfd);
    bstg_status_destroy(st);

    /* figures that stopped coming */
    st = newst(1, &wfd);
    report(wfd, mono() - 30, 0, 1ULL << 30, 0, 1ULL << 30, 0, 0);
    draw(st, buf, sizeof buf);
    ok(strstr(buf, "statvfs HUNG") != NULL && strstr(buf, "secs old") != NULL,
        "stale figures flagged");
    close(wfd);
    bstg_status_destroy(st);

    /* nothing heard yet */
    st = newst(1, &wfd);
    draw(st, buf, sizeof buf);
    ok(strstr(buf, "waiting for statvfs") != NULL, "waiting for the helper");
    close(wfd);

    /* no helper at all */
    close(st->disk.fd);
    st->disk.fd = -1;
    draw(st, buf, sizeof buf);
    ok(strstr(buf, "(no disk helper)") != NULL, "no disk helper");
    bstg_status_destroy(st);

    /* the helper does not outlive the dashboard */
    st = bstg_status_init(NFUNCS, 1);
    helper = st->disk.pid;
    ok(helper > 0, "helper started");
    ok(bstg_status_destroy(st) == 0 && kill(helper, 0) == -1,
        "destroy stops the helper");

    /* shared counters see forked workers */
    st = bstg_status_init(NFUNCS, 2);
    pid = fork();
    if (pid == 0) {
        bstg_status_op(st, 1, 2);
        bstg_status_op(st, 1, 2);
        _exit(0);
    }
    waitpid(pid, &status, 0);
    ok(bstg_status_opcount(st, 2) == 2 && st->child[1].lastop == 2,
        "forked worker's ops are counted");
    ok(st->child[0].lastop == -1, "idle worker has no op");
    bstg_status_destroy(st);

    return 0;
}
