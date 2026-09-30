/*
 * vim:ts=4:sw=4:expandtab
 *
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

#include <archive.h>
#include <archive_entry.h>
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/cdefs.h>

#include "bstg.h"

#include <cjson/cJSON.h>

__RCSID("$Id$");

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
    fprintf(stderr, "usage: clodagh [-n]\n");
    fprintf(stderr, "usage: clodagh [-v] archive-file ...\n");
    exit(1);
}


int
main(int argc, char *argv[])
{
    int ch, nflag, status, x, verbose;
    static bstg_funcs_t bfs;
    char *buf;

    verbose = nflag = 0;
    while ((ch = getopt(argc, argv, "nv")) != -1) {
        switch (ch) {
            case 'n':
                nflag = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case '?':
            default:
                usage();
        }
    }
    argv += optind;

    if (nflag) {
        printf("%lu\n", NFUNCS);
        return 0;
    }

    if (bstg_funcs_init(&bfs, verbose, 1048576)) {
        errx(1, "cannot init bfs");
    }

    buf = BSTGNULLCHECK(malloc(65536));

    for (x = 0; x < 128; x++) {
        pid_t pid;

        pid = fork();
        if (pid == 0) {
            srand(x);

            for (; *argv; argv++) {
                struct archive *a;
                struct archive_entry *entry;

                a = archive_read_new();
                archive_read_support_format_all(a);
                archive_read_support_filter_all(a);
                if (archive_read_open_filename(a, *argv, 10240)) {
                    _exit(2);
                }
                while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
                    size_t size;

                    size = archive_entry_size(entry);
                    buf = BSTGNULLCHECK(realloc(buf, size+1024));
                    if (archive_read_data(a, buf, size) == size) {
                        cJSON *ops;
                        cJSON *op;
                        unsigned id;

                        /* zero terminate the file from the archive */
                        buf[size] = '\0';

                        /* archives from mkpax builds before the libcjson
                         * migration stored entries as an XML plist; give a
                         * clear diagnosis instead of failing the JSON parse */
                        if (strncmp(buf, "<?xml", 5) == 0) {
                            warnx("archive entry is in the old proplib/XML "
                                "format; regenerate it with the current "
                                "mkpax\n");
                            _exit(2);
                        }

                        ops = BSTGNULLCHECK(cJSON_Parse(buf));
                        if (!cJSON_IsArray(ops)) {
                            cJSON_Delete(ops);
                            _exit(2);
                        }
                        cJSON_ArrayForEach(op, ops) {
                            if (!cJSON_IsNumber(op)) {
                                cJSON_Delete(ops);
                                _exit(2);
                            }
                            /* valueint is parse-time clamped to INT_MIN/INT_MAX,
                             * so this cast (unlike a double->unsigned cast) is
                             * well-defined even for a hostile/corrupt archive */
                            id = (unsigned)op->valueint;
                            (*bstg_fembot_funcs[id % NFUNCS]) (&bfs);
                        }
                        cJSON_Delete(ops);
                    }
                }
                archive_read_close(a);
                archive_read_free(a);
            }

            if (bstg_funcs_destroy(&bfs)) {
                errx(1, "cannot destroy bfs");
            }

            _exit(1);
        }
    }

    while(wait(&status) > 0);

    if (bstg_funcs_destroy(&bfs)) {
        errx(1, "cannot destroy bfs");
    }

    return 0;
}
