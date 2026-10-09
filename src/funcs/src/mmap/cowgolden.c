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

int fd, x;
char *area;
struct stat sbuf;
size_t len, n;

area = MAP_FAILED;
len = 0;
fd = open(BSTG_BASEDIR"/true", O_RDONLY|O_NONBLOCK|O_NOCTTY);
if ((fd >= 0) && (fstat(fd, &sbuf) == 0) && (sbuf.st_size > 0)) {
    len = sbuf.st_size;
    area = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
}
if (fd >= 0) {
    close(fd);
}

if (area != MAP_FAILED) {
    n = min(len, (size_t)getpagesize());
    if (!sigsetjmp(bstg_jmpbuf, 1)) {
        signal(SIGBUS, bstg_signalj);
        signal(SIGSEGV, bstg_signalj);
        for (x = 0; x < 100; x++) {
            memcpy(area, pf->pattern.iov_base, n);
            madvise(area, len, MADV_DONTNEED);
            memcpy(pf->buffer.iov_base, area, n);
        }
    }
    signal(SIGBUS, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);

    munmap(area, len);
}

__RCSID("$Id$");
