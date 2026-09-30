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

/* libarchive rocks */
#include <archive.h>
#include <archive_entry.h>

#include <cjson/cJSON.h>

/* BSD rocks too, I guess */
#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/cdefs.h>

#include "bstg.h"

__RCSID("$Id$");

#define AR_C    ((unsigned)(0x01))
#define AR_R    ((unsigned)(0x02))

#define NFUNCS ((unsigned long)((sizeof(bstg_fembot_funcs)/sizeof(bstg_fembot_funcs[0]))))


/* just add to make the code link -- bad API, I guess */
char *
bstg_pathstore_get()
{
    abort();
}

#include "fembotfuncs.h"

static bstg_flist_t flist;

void
write_archive(char *outname)
{
    struct archive *a;
    struct archive_entry *entry;
    size_t len;
    int i;

    a = archive_write_new();
    archive_write_add_filter_gzip(a);
    archive_write_set_format_pax_restricted(a);
    archive_write_open_filename(a, outname);
    entry = archive_entry_new();

    for (i = 0; i < 4096; i++) {
        int x;
        char filename[64];
        int ids[2048];
        cJSON *ops;
        char *json;

        for (x = 0; x < 2048; x++) {
            unsigned id;

            id = bstg_flist_get(&flist, arc4random());
            assert(id < NFUNCS);

            ids[x] = id;
        }

        ops = BSTGNULLCHECK(cJSON_CreateIntArray(ids, 2048));
        json = BSTGNULLCHECK(cJSON_PrintUnformatted(ops));
        len = strlen(json);

        snprintf(filename, sizeof filename, "%08X", i);
        archive_entry_set_pathname(entry, filename);
        archive_entry_set_size(entry, len);
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_write_header(a, entry);
        archive_write_data(a, json, len);
        archive_entry_clear(entry);

        cJSON_free(json);
        cJSON_Delete(ops);
    }

    archive_entry_free(entry);
    archive_write_close(a);
    archive_write_free(a);
}

void
read_archive(char *filename)
{
    struct archive *a;
    struct archive_entry *entry;
    char *buf;

    buf = BSTGNULLCHECK(malloc(262144));
    a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    if (archive_read_open_filename(a, filename, 10240)) {
        errx(1, "cannot read: %s", archive_error_string(a));
    }
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        size_t size;

        size = archive_entry_size(entry);
        buf = BSTGNULLCHECK(realloc(buf, size+1024));
        if (archive_read_data(a, buf, size) == size) {
            cJSON *ops;
            cJSON *op;

            /* zero terminate the file from the archive */
            buf[size] = '\0';

            /* archives from mkpax builds before the libcjson migration
             * stored entries as an XML plist; give a clear diagnosis
             * instead of failing the JSON parse */
            if (strncmp(buf, "<?xml", 5) == 0) {
                errx(1, "archive entry is in the old proplib/XML format; "
                    "regenerate it with the current mkpax");
            }

            ops = BSTGNULLCHECK(cJSON_Parse(buf));
            if (!cJSON_IsArray(ops)) {
                cJSON_Delete(ops);
                errx(1, "malformed archive entry: not a JSON array");
            }
            cJSON_ArrayForEach(op, ops) {
                if (!cJSON_IsNumber(op)) {
                    cJSON_Delete(ops);
                    errx(1, "malformed archive entry: non-numeric element");
                }
                /* valueint is parse-time clamped to INT_MIN/INT_MAX, so this
                 * cast (unlike a double->unsigned cast) is well-defined even
                 * for a hostile/corrupt archive */
                printf("%u\n", (unsigned)op->valueint);
            }
            cJSON_Delete(ops);
        }
    }
    free(buf);

    archive_read_close(a);
    archive_read_free(a);
}

static void
usage(int rc)
{
    fprintf(stderr, "usage: mkpax -c [-hv] [-S list] -f archive-file\n");
    fprintf(stderr, "       mkpax -c [-hv] [-S list] -J shards\n");
    fprintf(stderr, "       mkpax -r [-hv] -f archive-file\n");
    exit(rc);
}

int
main(int argc, char **argv)
{
    int rc;
    unsigned options;
    unsigned long optJ;
    char *farg, *optS;
    u_int32_t base;

    rc = options = 0;
    optJ = 1;
    optS = farg = NULL;
    while ((rc = getopt(argc, argv, "cf:hJ:rS:v")) != -1) {
        char *p;

        switch(rc) {
            case 'c':
                options |= AR_C;
                break;
            case 'f':
                farg = optarg;
                break;
            case 'h':
                usage(0);
                break;
            case 'J':
                optJ = strtoul(optarg, &p, 10);
                if (*p != 0) {
                    usage(1);
                }
                break;
            case 'r':
                options |= AR_R;
                break;
            case 'S':
                optS = optarg;
                break;
            case 'v':
                /* not used */
                break;
            default:
                usage(1);
        }
    }
    argv += optind;
    argc -= optind;

    if ((options == 0) || ((options & AR_R) && (options & AR_C))) {
        usage(1);
    }

    if (options & AR_R) {
        read_archive((farg == NULL ? "/dev/fd/0" : farg));
        exit(0);
    }

    /* TODO(kirk) clean this up */
    assert(options & AR_C);

    rc = bstg_flist_init(&flist, NFUNCS);
    assert(rc == 0);

    /* not required -- default will be 0 to NFUNCS-1 */
    if (optS) {
        rc = bstg_flist_import(&flist, optS);
        assert(rc == 0);
    }

    /* every shard needs at least one function, and J=0 would divide by 0 */
    if ((optJ == 0) || (optJ > flist.number)) {
        errx(1, "-J must be between 1 and %u", flist.number);
    }

    /* shuffle the current ordering */
    rc = bstg_flist_shuffle(&flist);
    assert(rc == 0);

    /* shard the output files */
    base = 0;
    while (base < flist.number) {
        u_int32_t count;

        /* the number of items we will put in the shard */
        count = flist.number/optJ;

        /* put any remainders in the last shard */
        if ((base + count + count) > flist.number) {
            count = flist.number - base;
        }

        /* setup the shard boundaries with our set */
        rc = bstg_flist_set(&flist, base, base + count);
        if (rc) {
            errx(1, "%u %u %u", base, count, base+count);
        }

        if (optJ == 1) {
            /* default is just one shard, so use given filename */
            write_archive((farg == NULL ? "/dev/fd/1" : farg));
        } else { 
            char filename[120];

            snprintf(filename, sizeof filename, "./%d.tar.gz", base);
            write_archive(filename);
        }

        base += count;
    }
    return 0;
}

