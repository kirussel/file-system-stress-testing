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

pid_t childpid;
char *token[] = { NULL, NULL };
char *libpath;

token[0] = bstg_pathstore_get();
libpath = dirname(strdup(bstg_pathstore_get()));
if ((childpid = fork()) > 0) {
    int status;

    /* parent should busywait for the child to terminate */
    bstg_waitpid(childpid, &status, 60);
} else if (childpid == 0) {
    extern char **environ;
    char *buf;
    int len;

    /* add envvar for LD_LIBRARY_PATH */
    if ((buf = malloc(len = (25 + strlen(libpath)))) != NULL) {
        snprintf(buf, len, "LD_LIBRARY_PATH=%s", libpath);
        putenv(buf);
    }

    putenv("LD_PRELOAD=libfembotisnothere.so.13");

    bstg_preexec(ps);
    execve(token[0], token, environ);
    _exit(1);
}

__RCSID("$Id$");
