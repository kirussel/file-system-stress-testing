#!/bin/sh
# shellcheck shell=sh
#
# Copyright 2026 The file-system-stress-testing Authors
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
# BSD install(1) shim for Linux. GNU install has no "-f flags" option, so
# strip it, run the real install, then emulate the "uchg" flag with
# "chattr +i". Like BSD install, an existing immutable target is unlocked
# first so it can be replaced.
#
# REAL_INSTALL is the GNU install to run (default /usr/bin/install).
# SUDO runs chattr when not root (default sudo).

set -f

REAL_INSTALL="${REAL_INSTALL:-/usr/bin/install}"
if [ "$(id -u)" -eq 0 ]; then
  SUDO=""
else
  SUDO="${SUDO-sudo}"
fi

flags=""
want=""
operands=""
for arg do
  shift
  case "${want}" in
    f)  want=""; flags="${arg}"; continue ;;
    v)  want=""; set -- "$@" "${arg}"; continue ;;
  esac
  case "${arg}" in
    -f)        want=f; continue ;;
    -f?*)      flags="${arg#-f}"; continue ;;
    -[BgmMoST]) want=v ;;
    -*)        ;;
    *)         operands="${operands} ${arg}" ;;
  esac
  set -- "$@" "${arg}"
done

# the last operand is the destination; any before it are sources
dest=""
srcs=""
for op in ${operands}; do
  if [ -n "${dest}" ]; then
    srcs="${srcs} ${dest}"
  fi
  dest="${op}"
done

# list the files install will write
targets=""
if [ -d "${dest}" ]; then
  for src in ${srcs}; do
    targets="${targets} ${dest}/$(basename "${src}")"
  done
else
  targets="${dest}"
fi

is_immutable()
{
  attrs="$(lsattr -d -- "${1}" 2>/dev/null)" || return 1
  case "${attrs%% *}" in
    *i*) return 0 ;;
  esac
  return 1
}

for t in ${targets}; do
  if is_immutable "${t}" ; then
    ${SUDO} chattr -i -- "${t}" || exit 1
  fi
done

"${REAL_INSTALL}" "$@" || exit

case ",${flags}," in
  *,uchg,*|*,schg,*)
    for t in ${targets}; do
      if ! ${SUDO} chattr +i -- "${t}" ; then
        printf "%s: chattr +i %s failed\n" "$0" "${t}" >&2
        exit 1
      fi
    done
    ;;
esac

exit 0
