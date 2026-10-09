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
size_t len;

sbuf.st_size = 0;
fstat(fd = bstg_fdstore_get(ps), &sbuf);
len = sbuf.st_size;
if (len < pf->pattern.iov_len)
    len = pf->pattern.iov_len;

area = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

if (area != MAP_FAILED) {
    if (!sigsetjmp(bstg_jmpbuf, 1)) {
        signal(SIGBUS, bstg_signalj);
        signal(SIGSEGV, bstg_signalj);
        memcpy(area, pf->pattern.iov_base, pf->pattern.iov_len);
    }
    signal(SIGBUS, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);

    msync(area, len, MS_ASYNC);
    munmap(area, len);
}

__RCSID("$Id$");
