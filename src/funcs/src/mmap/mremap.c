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

#if defined(linux) || (defined(__FreeBSD__) && defined(MAP_EXCL))
#if defined(linux) && !defined(MREMAP_MAYMOVE)
#define MREMAP_MAYMOVE 1
extern void *mremap(void *, size_t, size_t, int, ...);
#endif
int fd;
char *area, *moved;
size_t len, newlen, pg;

fd = bstg_fdstore_get(ps);
len = 16384;
newlen = pf->pattern.iov_len;
pg = getpagesize();

area = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

if (area != MAP_FAILED) {
    if (!sigsetjmp(bstg_jmpbuf, 1)) {
        signal(SIGBUS, bstg_signalj);
        signal(SIGSEGV, bstg_signalj);
        memcpy(area, pf->pattern.iov_base, pg);
    }
    signal(SIGBUS, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);

    ftruncate(fd, 0);

#ifdef linux
    moved = mremap(area, len, newlen, MREMAP_MAYMOVE);
#else
    /* FreeBSD: try to extend in place with the next part of the same file */
    moved = mmap(area + len, newlen - len, PROT_READ|PROT_WRITE,
        MAP_SHARED|MAP_FIXED|MAP_EXCL, fd, (off_t)len);
    if (moved != MAP_FAILED) {
        moved = area;
    } else {
        /* adjacent range is taken: map the whole file again elsewhere */
        moved = mmap(NULL, newlen, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        if (moved != MAP_FAILED) {
            munmap(area, len);
        }
    }
#endif
    if (moved != MAP_FAILED) {
        area = moved;
        len = newlen;
    }

    if (!sigsetjmp(bstg_jmpbuf, 1)) {
        signal(SIGBUS, bstg_signalj);
        signal(SIGSEGV, bstg_signalj);
        memcpy(area, pf->pattern.iov_base, pg);
    }
    signal(SIGBUS, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);

    if (!sigsetjmp(bstg_jmpbuf, 1)) {
        signal(SIGBUS, bstg_signalj);
        signal(SIGSEGV, bstg_signalj);
        memcpy(pf->buffer.iov_base, area + len - pg, pg);
    }
    signal(SIGBUS, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);

    munmap(area, len);
}
#endif

__RCSID("$Id$");
