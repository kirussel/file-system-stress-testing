#!/bin/ksh
#
# vim:et:ft=sh:ts=4:sw=4:expandtab
#
# Copyright 2016 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
EXE2TEST=${DIR?}/mkpax

test_usage()
{
    ${EXE2TEST} -h > stdout 2> stderr
    assertTrue 'returns 0' "${?}"

    cmp stdout /dev/null
    assertTrue 'no stdout' "${?}"

    diff stderr - <<"HERE"
usage: mkpax -c [-hv] [-S list] -f archive-file
       mkpax -c [-hv] [-S list] -J shards
       mkpax -r [-hv] -f archive-file
HERE
    assertTrue 'usage in stderr' "${?}"
}

test_bad_argument()
{
    ${EXE2TEST} -9 > stdout 2> stderr
    assertFalse 'returns !0' "${?}"

    cmp stdout /dev/null
    assertTrue 'no stdout' "${?}"

    diff stderr - <<"HERE"
mkpax: illegal option -- 9
usage: mkpax -c [-hv] [-S list] -f archive-file
       mkpax -c [-hv] [-S list] -J shards
       mkpax -r [-hv] -f archive-file
HERE
    assertTrue 'usage in stderr' "${?}"
}

test_two_archives()
{
    ${EXE2TEST} -c -J 2 > stdout 2> stderr
    assertTrue 'returns 0' "${?}"

    cmp stdout /dev/null
    assertTrue 'no stdout' "${?}"

    cmp stderr /dev/null
    assertTrue 'no stderr' "${?}"

    assertEquals "4096" "$(tar tzf 0.tar.gz \
                            | wc -l \
                            | xargs printf "%d")"
    assertEquals "2048" "$(tar xzOf 0.tar.gz 00000EBB \
                            | fgrep integer \
                            | wc -l \
                            | xargs printf "%d")"
    tar xzOf 0.tar.gz | fgrep integer | sort -u > 0.txt
    assertTrue 'tar returns 0' "${?}"
    assertEquals "51" "$(wc -l < 0.txt \
                            | xargs printf "%d")"

    assertEquals "4096" "$(tar tzf 51.tar.gz \
                            | wc -l \
                            | xargs printf "%d")"
    assertEquals "2048" "$(tar xzOf 51.tar.gz 00000EBB \
                            | fgrep integer \
                            | wc -l \
                            | xargs printf "%d")"
    tar xzOf 51.tar.gz | fgrep integer | sort -u > 51.txt
    assertTrue 'tar returns 0' "${?}"
    assertEquals "51" "$(wc -l < 51.txt \
                            | xargs printf "%d")"

    assertEquals "102" "$(sort -u 0.txt 51.txt \
                            | wc -l \
                            | xargs printf "%d")"
}

test_five_functions()
{
    ${EXE2TEST} -c -S "0 1 2 3 4" -J 2 > stdout 2> stderr
    assertTrue 'returns 0' "${?}"

    cmp stdout /dev/null
    assertTrue 'no stdout' "${?}"

    cmp stderr /dev/null
    assertTrue 'no stderr' "${?}"

    tar xzOf 0.tar.gz | fgrep integer | sort -u > 0.txt
    assertTrue 'tar returns 0' "${?}"
    assertEquals "2" "$(wc -l < 0.txt \
                            | xargs printf "%d")"

    tar xzOf 2.tar.gz | fgrep integer | sort -u > 2.txt
    assertTrue 'tar returns 0' "${?}"
    assertEquals "3" "$(wc -l < 2.txt \
                            | xargs printf "%d")"

    assertEquals "5" "$(sort -u 0.txt 2.txt \
                            | wc -l \
                            | xargs printf "%d")"
}


setUp()
{
    cd ${SHUNIT_TMPDIR?}
}

tearDown()
{
    rm -f *txt *.tar.gz stdout stderr
}

. shunit2
