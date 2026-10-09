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

int fd;
char *area;
struct stat sbuf;
size_t len, pg;

sbuf.st_size = 0;
fstat(fd = bstg_fdstore_get(ps), &sbuf);
len = pf->pattern.iov_len;
pg = getpagesize();

area = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);

if (area != MAP_FAILED) {
    if ((size_t)sbuf.st_size < len) {
        if (!sigsetjmp(bstg_jmpbuf, 1)) {
            signal(SIGBUS, bstg_signalj);
            signal(SIGSEGV, bstg_signalj);
            memcpy(pf->buffer.iov_base, area + sbuf.st_size, 1);
        }
        signal(SIGBUS, SIG_DFL);
        signal(SIGSEGV, SIG_DFL);
    }

    if (!sigsetjmp(bstg_jmpbuf, 1)) {
        signal(SIGBUS, bstg_signalj);
        signal(SIGSEGV, bstg_signalj);
        memcpy(pf->buffer.iov_base, area + len - pg, pg);
    }
    signal(SIGBUS, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);

    munmap(area, len);
}

__RCSID("$Id$");
