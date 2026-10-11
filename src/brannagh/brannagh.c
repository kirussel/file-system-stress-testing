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

#ifndef BSTG_BASEDIR
#error You need to define the mount point used during your test execution.
#endif

#include "fembot.h"
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/cdefs.h>

#ifdef __RCSID
__RCSID("$Id$");
#endif

#define NPATHS ((sizeof(bstg_pathstore)/sizeof(bstg_pathstore[0])))
#define NFUNCS ((unsigned long)((sizeof(bstg_fembot_funcs)/sizeof(bstg_fembot_funcs[0]))))

const char *const bstg_pathstore[] = {
#include "pathstore.h"
};


char *
bstg_pathstore_get()
{
    return (char *)bstg_pathstore[rand() % NPATHS];
}

#include "fembotfuncs.h"

void
usage(void)
{
    fprintf(stderr, "usage: brannagh [-n]\n");
    fprintf(stderr, "usage: brannagh [-s | -v] [-f 0-%lu]\n", NFUNCS);
    exit(1);
}


int
main(int argc, char *argv[])
{
    int ch, nflag, farg, status, x, verbose, sflag;
    static bstg_funcs_t bfs;
    bstg_status_t *st;
    pid_t pid;

    nflag = 0;
    farg = -1;
    verbose = 0;
    sflag = 0;
    while ((ch = getopt(argc, argv, "f:nsv")) != -1) {
        char *p;

        switch (ch) {
            case 'f':
                farg = strtol(optarg, &p, 10);
                if (*p != 0) {
                    usage();
                }
                break;
            case 'n':
                nflag = 1;
                break;
            case 's':
                sflag = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case '?':
            default:
                usage();
        }
    }

    if (sflag && verbose) {
        warnx("-s and -v are mutually exclusive");
        usage();
    }

    if (nflag) {
        printf("%lu\n", NFUNCS);
        return 0;
    }

    //  if (farg < -1 || farg >= NFUNCS) {
    //    usage();
    //  }

    if (bstg_funcs_init(&bfs, verbose, 1048576)) {
        errx(1, "cannot init bfs");
    }

    /* the dashboard is only for a human at a terminal */
    if (sflag && !isatty(STDOUT_FILENO)) {
        warnx("stdout is not a tty: -s ignored");
        sflag = 0;
    }

    /* without -s the workers run exactly as before: no counters, blocking wait */
    st = NULL;
    if (sflag) {
        st = BSTGNULLCHECK(bstg_status_init(NFUNCS, 37));
    }

    for (x = 0; x < 37; x++) {
        int func = 0;

        pid = fork();
        if (pid == 0) {
            srand(x);
            func = x;

            for (;;) {
                bstg_status_op(st, x, func % NFUNCS);
                (*bstg_fembot_funcs[func++ % NFUNCS]) (&bfs);
            }

            if (bstg_funcs_destroy(&bfs)) {
                errx(1, "cannot destroy bfs");
            }

            _exit(1);
        }
        bstg_status_child(st, x, pid);
    }

    if (sflag) {
        for (;;) {
            while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
                bstg_status_reaped(st, pid, status);
            }
            if (pid == -1 && errno != EINTR) {
                break;
            }
            bstg_status_draw(st, stdout, bstg_fembot_names, "brannagh");
            sleep(1);
        }
        bstg_status_draw(st, stdout, bstg_fembot_names, "brannagh");
        bstg_status_destroy(st);
    } else {
        while(wait(&status) > 0);
    }

    if (bstg_funcs_destroy(&bfs)) {
        errx(1, "cannot destroy bfs");
    }

    return 0;
}
