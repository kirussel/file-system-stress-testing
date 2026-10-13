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
 * libcheck tests for the -s dashboard (bstg_status.c). The disk line is
 * driven through a pipe we own instead of the real statvfs helper, so the
 * figures are exact and nothing waits on the clock.
 */

#include <check.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "fembot.h"
#include "bstg.h"

#define NFUNCS  4

static const char *const names[NFUNCS] = {
    "a/one", "b/two", "c/three", "d/four"
};

static bstg_status_t *st;

static time_t
mono(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

/* stop the real disk helper and read from a pipe the test controls */
static int
fakedisk(bstg_status_t *s)
{
    int fds[2];

    kill(s->disk.pid, SIGKILL);
    waitpid(s->disk.pid, NULL, 0);
    close(s->disk.fd);
    if (pipe(fds) != 0) {
        return -1;
    }
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    s->disk.fd = fds[0];
    s->disk.pid = -1;
    s->disk.len = 0;
    s->disk.have = 0;
    return fds[1];
}

static void
diskreport(int wfd, time_t t, int err, unsigned long long size,
    unsigned long long used, unsigned long long avail,
    unsigned long long files, unsigned long long ffree)
{
    char line[256];
    int n;

    n = snprintf(line, sizeof line, "{\"t\":%lld,\"err\":%d,\"size\":%llu,"
        "\"used\":%llu,\"avail\":%llu,\"files\":%llu,\"ffree\":%llu}\n",
        (long long)t, err, size, used, avail, files, ffree);
    ck_assert_int_eq(write(wfd, line, n), n);
}

/* draw into buf; returns buf */
static char *
draw(char *buf, size_t len)
{
    FILE *out;

    memset(buf, 0, len);
    out = fmemopen(buf, len, "w");
    ck_assert_ptr_nonnull(out);
    bstg_status_draw(st, out, names, "check");
    fclose(out);
    return buf;
}

static void
setup(void)
{
    st = bstg_status_init(NFUNCS, 3);
    ck_assert_ptr_nonnull(st);
}

static void
teardown(void)
{
    bstg_status_destroy(st);
    st = NULL;
}

START_TEST(test_null_status_is_noop)
{
    bstg_status_child(NULL, 0, 1);
    bstg_status_op(NULL, 0, 0);
    bstg_status_reaped(NULL, 1, 0);
    bstg_status_draw(NULL, stdout, names, "check");
    ck_assert_int_eq(bstg_status_destroy(NULL), 0);
}
END_TEST

START_TEST(test_init)
{
    unsigned i;

    ck_assert_uint_eq(st->magic, BSTG_STATUS_MAGIC);
    ck_assert_uint_eq(st->nfuncs, NFUNCS);
    ck_assert_uint_eq(st->nchildren, 3);
    ck_assert_uint_ge(st->stride, NFUNCS);
    ck_assert_int_eq(st->lastdeath, -1);
    ck_assert_uint_eq(st->deaths, 0);
    for (i = 0; i < 3; i++) {
        ck_assert_int_eq(st->child[i].lastop, -1);
    }
    /* nothing started, so nothing to wait for */
    ck_assert(bstg_status_done(st));
    bstg_status_child(st, 0, 1000);
    ck_assert(!bstg_status_done(st));
}
END_TEST

START_TEST(test_opcount)
{
    bstg_status_op(st, 0, 1);
    bstg_status_op(st, 0, 1);
    bstg_status_op(st, 2, 1);
    bstg_status_op(st, 1, 3);

    ck_assert_uint_eq(bstg_status_opcount(st, 0), 0);
    ck_assert_uint_eq(bstg_status_opcount(st, 1), 3);
    ck_assert_uint_eq(bstg_status_opcount(st, 3), 1);
    ck_assert_int_eq(st->child[0].lastop, 1);
    ck_assert_int_eq(st->child[1].lastop, 3);
    ck_assert_int_ne(st->child[1].lastop_at, 0);
}
END_TEST

START_TEST(test_opcount_out_of_range)
{
    bstg_status_op(st, 0, NFUNCS);      /* unknown op */
    bstg_status_op(st, 3, 0);           /* unknown worker */
    ck_assert_uint_eq(bstg_status_opcount(st, NFUNCS), 0);
    ck_assert_uint_eq(bstg_status_opcount(st, 0), 0);
}
END_TEST

/* ops counted in forked workers show up in the parent */
START_TEST(test_opcount_shared_with_forked_workers)
{
    pid_t pid;
    int status;

    pid = fork();
    ck_assert_int_ge(pid, 0);
    if (pid == 0) {
        bstg_status_op(st, 1, 2);
        bstg_status_op(st, 1, 2);
        _exit(0);
    }
    ck_assert_int_eq(waitpid(pid, &status, 0), pid);
    ck_assert_uint_eq(bstg_status_opcount(st, 2), 2);
}
END_TEST

START_TEST(test_reaped_and_done)
{
    pid_t pid[3];
    int status;
    unsigned i;

    for (i = 0; i < 3; i++) {
        pid[i] = fork();
        ck_assert_int_ge(pid[i], 0);
        if (pid[i] == 0) {
            bstg_status_op(st, i, 1);
            if (i == 2) {
                raise(SIGKILL);
            }
            _exit(i == 1 ? 7 : 0);
        }
        bstg_status_child(st, i, pid[i]);
    }
    for (i = 0; i < 3; i++) {
        ck_assert(!bstg_status_done(st));
        pid_t p = wait(&status);
        bstg_status_reaped(st, p, status);
    }
    ck_assert(bstg_status_done(st));
    ck_assert_uint_eq(st->deaths, 3);
    ck_assert_int_ge(st->lastdeath, 0);

    ck_assert(st->child[1].dead);
    ck_assert(WIFEXITED(st->child[1].wstatus));
    ck_assert_int_eq(WEXITSTATUS(st->child[1].wstatus), 7);
    ck_assert_int_eq(st->child[2].lastop, 1);
    ck_assert(WIFSIGNALED(st->child[2].wstatus));
    ck_assert_int_eq(WTERMSIG(st->child[2].wstatus), SIGKILL);

    /* an unknown pid is ignored */
    bstg_status_reaped(st, 1, 0);
    ck_assert_uint_eq(st->deaths, 3);
}
END_TEST

START_TEST(test_draw_header_and_op_mix)
{
    char buf[8192];
    char *one, *three, *two;
    unsigned i;

    bstg_status_child(st, 0, 1000);
    for (i = 0; i < 5; i++) {
        bstg_status_op(st, 0, 2);
    }
    for (i = 0; i < 3; i++) {
        bstg_status_op(st, 0, 0);
    }
    bstg_status_op(st, 0, 1);
    draw(buf, sizeof buf);

    ck_assert_ptr_nonnull(strstr(buf, "check (fsst)"));
    ck_assert_ptr_nonnull(strstr(buf, "total ops: 9"));
    ck_assert_ptr_nonnull(strstr(buf, "workers  : 1/3 alive"));
    ck_assert_ptr_nonnull(strstr(buf, "deaths   : 0"));
    /* repaints in place: home the cursor, erase the tail, erase below */
    ck_assert(strncmp(buf, "\033[H", 3) == 0);
    ck_assert_ptr_nonnull(strstr(buf, "\033[K"));
    ck_assert_ptr_nonnull(strstr(buf, "\033[J"));

    /* sorted by count, descending; ops never started are omitted */
    three = strstr(buf, "c/three");
    one = strstr(buf, "a/one");
    two = strstr(buf, "b/two");
    ck_assert_ptr_nonnull(three);
    ck_assert_ptr_nonnull(one);
    ck_assert_ptr_nonnull(two);
    ck_assert(three < one && one < two);
    ck_assert_ptr_null(strstr(buf, "d/four"));
    ck_assert_ptr_nonnull(strstr(buf, "55.6%"));
}
END_TEST

START_TEST(test_draw_last_death)
{
    char buf[8192];
    pid_t pid;
    int status;

    pid = fork();
    ck_assert_int_ge(pid, 0);
    if (pid == 0) {
        bstg_status_op(st, 0, 3);
        raise(SIGKILL);
        _exit(0);
    }
    bstg_status_child(st, 0, pid);
    ck_assert_int_eq(waitpid(pid, &status, 0), pid);
    bstg_status_reaped(st, pid, status);
    draw(buf, sizeof buf);

    ck_assert_ptr_nonnull(strstr(buf, "deaths   : 1"));
    ck_assert_ptr_nonnull(strstr(buf, "Killed"));
    ck_assert_ptr_nonnull(strstr(buf, "in d/four"));
    ck_assert_ptr_nonnull(strstr(buf, "workers  : 0/3 alive"));
}
END_TEST

START_TEST(test_draw_stalled_worker)
{
    char buf[8192];

    bstg_status_child(st, 0, 4242);
    bstg_status_child(st, 1, 4243);
    bstg_status_op(st, 0, 1);
    bstg_status_op(st, 1, 2);
    st->child[0].lastop_at = mono() - 60;   /* stuck for a minute */
    draw(buf, sizeof buf);

    ck_assert_ptr_nonnull(strstr(buf, "pid 4242 in b/two for "));
    ck_assert_ptr_null(strstr(buf, "pid 4243 in"));
}
END_TEST

START_TEST(test_draw_stalled_list_is_capped)
{
    bstg_status_t *big;
    FILE *out;
    char buf[16384];
    unsigned i;

    big = bstg_status_init(NFUNCS, 14);
    ck_assert_ptr_nonnull(big);
    for (i = 0; i < 14; i++) {
        bstg_status_child(big, i, 5000 + i);
        bstg_status_op(big, i, 0);
        big->child[i].lastop_at = mono() - 60;
    }
    memset(buf, 0, sizeof buf);
    out = fmemopen(buf, sizeof buf, "w");
    ck_assert_ptr_nonnull(out);
    bstg_status_draw(big, out, names, "check");
    fclose(out);
    bstg_status_destroy(big);

    ck_assert_ptr_nonnull(strstr(buf, "pid 5009 in a/one"));
    ck_assert_ptr_null(strstr(buf, "pid 5010 in a/one"));
    ck_assert_ptr_nonnull(strstr(buf, "... and 4 more"));
}
END_TEST

START_TEST(test_draw_disk_figures)
{
    char buf[8192];
    int wfd = fakedisk(st);

    ck_assert_int_ge(wfd, 0);
    /* 1 GiB, 256 MiB used, 768 MiB avail; 1000 inodes, 900 free */
    diskreport(wfd, mono(), 0, 1ULL << 30, 256ULL << 20, 768ULL << 20,
        1000, 900);
    draw(buf, sizeof buf);
    close(wfd);

    ck_assert_ptr_nonnull(strstr(buf, "disk     : " BSTG_BASEDIR));
    ck_assert_ptr_nonnull(strstr(buf, "size 1.0G"));
    ck_assert_ptr_nonnull(strstr(buf, "used 256M"));
    ck_assert_ptr_nonnull(strstr(buf, "avail 768M"));
    ck_assert_ptr_nonnull(strstr(buf, "25%"));          /* used/(used+avail) */
    ck_assert_ptr_nonnull(strstr(buf, "inodes 10%"));
    ck_assert_ptr_null(strstr(buf, "HUNG"));
}
END_TEST

START_TEST(test_draw_disk_rounds_percent_up)
{
    char buf[8192];
    int wfd = fakedisk(st);

    ck_assert_int_ge(wfd, 0);
    /* 1 of 3 units used: 33.3% shows as 34%, like df */
    diskreport(wfd, mono(), 0, 3000, 1000, 2000, 0, 0);
    draw(buf, sizeof buf);
    close(wfd);

    ck_assert_ptr_nonnull(strstr(buf, "34%"));
    ck_assert_ptr_null(strstr(buf, "inodes"));  /* fs without inodes */
}
END_TEST

START_TEST(test_draw_disk_newest_report_wins)
{
    char buf[8192];
    int wfd = fakedisk(st);

    ck_assert_int_ge(wfd, 0);
    diskreport(wfd, mono(), 0, 1ULL << 30, 0, 1ULL << 30, 0, 0);
    diskreport(wfd, mono(), 0, 2ULL << 30, 0, 2ULL << 30, 0, 0);
    draw(buf, sizeof buf);
    close(wfd);

    ck_assert_ptr_nonnull(strstr(buf, "size 2.0G"));
}
END_TEST

START_TEST(test_draw_disk_error)
{
    char buf[8192];
    int wfd = fakedisk(st);

    ck_assert_int_ge(wfd, 0);
    diskreport(wfd, mono(), 5, 0, 0, 0, 0, 0);     /* EIO */
    draw(buf, sizeof buf);
    close(wfd);

    ck_assert_ptr_nonnull(strstr(buf, "unavailable: "));
}
END_TEST

START_TEST(test_draw_disk_hung)
{
    char buf[8192];
    int wfd = fakedisk(st);

    ck_assert_int_ge(wfd, 0);
    diskreport(wfd, mono() - 30, 0, 1ULL << 30, 0, 1ULL << 30, 0, 0);
    draw(buf, sizeof buf);
    close(wfd);

    ck_assert_ptr_nonnull(strstr(buf, "statvfs HUNG"));
    ck_assert_ptr_nonnull(strstr(buf, "secs old"));
}
END_TEST

START_TEST(test_draw_disk_waiting)
{
    char buf[8192];
    int wfd = fakedisk(st);

    ck_assert_int_ge(wfd, 0);
    draw(buf, sizeof buf);          /* the helper has said nothing yet */
    close(wfd);

    ck_assert_ptr_nonnull(strstr(buf, "waiting for statvfs"));
}
END_TEST

START_TEST(test_draw_disk_ignores_garbage)
{
    char buf[8192];
    int wfd = fakedisk(st);

    ck_assert_int_ge(wfd, 0);
    ck_assert_int_eq(write(wfd, "not json\n", 9), 9);
    draw(buf, sizeof buf);
    close(wfd);

    ck_assert_ptr_nonnull(strstr(buf, "waiting for statvfs"));
}
END_TEST

START_TEST(test_draw_no_disk_helper)
{
    char buf[8192];
    int wfd = fakedisk(st);

    close(wfd);
    close(st->disk.fd);
    st->disk.fd = -1;
    draw(buf, sizeof buf);

    ck_assert_ptr_nonnull(strstr(buf, "(no disk helper)"));
}
END_TEST

/* the real helper reports on the real BSTG_BASEDIR within a moment */
START_TEST(test_draw_real_disk_helper)
{
    char buf[8192];
    int i;

    for (i = 0; i < 50; i++) {
        draw(buf, sizeof buf);
        if (strstr(buf, "waiting for statvfs") == NULL) {
            break;
        }
        usleep(100000);
    }
    ck_assert_ptr_nonnull(strstr(buf, "disk     : " BSTG_BASEDIR));
    ck_assert(strstr(buf, " size ") != NULL ||
        strstr(buf, "unavailable") != NULL);
}
END_TEST

/* the helper must not outlive the dashboard */
START_TEST(test_destroy_stops_helper)
{
    pid_t helper = st->disk.pid;

    ck_assert_int_gt(helper, 0);
    ck_assert_int_eq(bstg_status_destroy(st), 0);
    st = NULL;
    ck_assert_int_eq(kill(helper, 0), -1);      /* reaped, so ESRCH */
}
END_TEST

static Suite *
status_suite(void)
{
    Suite *s = suite_create("bstg_status");
    TCase *tc = tcase_create("dashboard");
    TCase *td = tcase_create("disk");

    tcase_add_test(tc, test_null_status_is_noop);
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, test_init);
    tcase_add_test(tc, test_opcount);
    tcase_add_test(tc, test_opcount_out_of_range);
    tcase_add_test(tc, test_opcount_shared_with_forked_workers);
    tcase_add_test(tc, test_reaped_and_done);
    tcase_add_test(tc, test_draw_header_and_op_mix);
    tcase_add_test(tc, test_draw_last_death);
    tcase_add_test(tc, test_draw_stalled_worker);
    tcase_add_test(tc, test_draw_stalled_list_is_capped);
    suite_add_tcase(s, tc);

    tcase_set_timeout(td, 20);
    tcase_add_checked_fixture(td, setup, teardown);
    tcase_add_test(td, test_draw_disk_figures);
    tcase_add_test(td, test_draw_disk_rounds_percent_up);
    tcase_add_test(td, test_draw_disk_newest_report_wins);
    tcase_add_test(td, test_draw_disk_error);
    tcase_add_test(td, test_draw_disk_hung);
    tcase_add_test(td, test_draw_disk_waiting);
    tcase_add_test(td, test_draw_disk_ignores_garbage);
    tcase_add_test(td, test_draw_no_disk_helper);
    tcase_add_test(td, test_draw_real_disk_helper);
    tcase_add_test(td, test_destroy_stops_helper);
    suite_add_tcase(s, td);
    return s;
}

int
main(void)
{
    SRunner *sr = srunner_create(status_suite());
    int failed;

    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
