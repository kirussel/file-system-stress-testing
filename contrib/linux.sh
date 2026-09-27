#!/bin/sh
#
# Copyright 2026 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -eu

echo "== Checking prerequisites =="

MISSING_HINTS=""

C_COMPILER="${CC:-cc}"
# $CC may carry flags (e.g. "gcc -m32"); only check the command itself.
C_COMPILER_BIN="${C_COMPILER%% *}"
if ! command -v "$C_COMPILER_BIN" >/dev/null 2>&1; then
  MISSING_HINTS="$MISSING_HINTS
  - C compiler ('$C_COMPILER_BIN' not found). Hint: apt-get install -y gcc"
fi

MAKE_CMD=""
if command -v gmake >/dev/null 2>&1; then
  MAKE_CMD="gmake"
elif command -v make >/dev/null 2>&1; then
  if make --version 2>/dev/null | grep -q "GNU Make"; then
    MAKE_CMD="make"
  fi
fi

if [ -z "$MAKE_CMD" ]; then
  MISSING_HINTS="$MISSING_HINTS
  - GNU make. Hint: apt-get install -y make"
fi

if ! command -v prove >/dev/null 2>&1; then
  MISSING_HINTS="$MISSING_HINTS
  - prove. Hint: apt-get install -y perl"
fi

KSH_BIN=""
if [ -x /bin/ksh ]; then
  KSH_BIN=/bin/ksh
elif command -v ksh93 >/dev/null 2>&1; then
  KSH_BIN="$(command -v ksh93)"
elif command -v ksh >/dev/null 2>&1; then
  KSH_BIN="$(command -v ksh)"
elif command -v pdksh >/dev/null 2>&1; then
  KSH_BIN="$(command -v pdksh)"
fi

if [ -z "$KSH_BIN" ]; then
  MISSING_HINTS="$MISSING_HINTS
  - ksh (needed by the src/funcs code generators). Hint: apt-get install -y ksh"
fi

if ! command -v git >/dev/null 2>&1; then
  MISSING_HINTS="$MISSING_HINTS
  - git. Hint: apt-get install -y git"
fi

if [ -n "$MISSING_HINTS" ]; then
  echo "Missing prerequisites:"
  echo "$MISSING_HINTS"
  exit 1
fi

PREFIX="${PREFIX:-/usr/local}"

echo "== Checking libtap =="

if [ -f "$PREFIX/include/tap.h" ]; then
  if grep -q "plan_tests" "$PREFIX/include/tap.h"; then
    echo "libtap OK"
  else
    echo "Error: $PREFIX/include/tap.h looks like the wrong (zorgnax) libtap fork."
    echo "It uses plan()/tap_plan()/ok_at_loc() instead of plan_tests()/ok()."
    echo "Please remove $PREFIX/include/tap.h and $PREFIX/lib/libtap.* before re-running."
    exit 1
  fi
else
  BUILD_HINTS=""
  for tool in autoconf automake libtool; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      BUILD_HINTS="$BUILD_HINTS $tool"
    fi
  done
  
  if [ -n "$BUILD_HINTS" ]; then
    echo "Missing build tools for libtap: $BUILD_HINTS"
    echo "Hint: apt-get install -y autoconf automake libtool"
    exit 1
  fi
  
  TMP="${TMPDIR:-$HOME/tmp}"
  TAP_DIR="$TMP/libtap"
  
  echo "Building libtap in $TAP_DIR..."
  # Check for .git, not just the directory, so a previous interrupted clone
  # (leaving an empty or partial directory behind) gets retried instead of
  # silently skipped.
  if [ ! -d "$TAP_DIR/.git" ]; then
    rm -rf "$TAP_DIR"
    mkdir -p "$TMP"
    git clone https://github.com/pozorvlak/libtap.git "$TAP_DIR"
  else
    echo "Reusing existing libtap clone at $TAP_DIR"
  fi

  (
    cd "$TAP_DIR"
    sh bootstrap.sh
    ./configure --prefix="$PREFIX"
    make
  )

  # Walk up to the nearest existing ancestor of $PREFIX to decide whether we
  # can write there -- $PREFIX itself may not exist yet, which would
  # otherwise look "not writable" and force sudo unnecessarily.
  CHECK_DIR="$PREFIX"
  while [ ! -e "$CHECK_DIR" ]; do
    CHECK_DIR="$(dirname "$CHECK_DIR")"
  done

  NEED_SUDO=0
  if [ "$(id -u)" -ne 0 ] && [ ! -w "$CHECK_DIR" ]; then
    NEED_SUDO=1
    if ! command -v sudo >/dev/null 2>&1; then
      echo "Error: $PREFIX is not writable and sudo is not available."
      echo "Re-run as a user who can write to $PREFIX, or set PREFIX to a writable location."
      exit 1
    fi
  fi

  if [ "$NEED_SUDO" -eq 1 ]; then
    ( cd "$TAP_DIR" && sudo make install )
    sudo ldconfig
  else
    ( cd "$TAP_DIR" && make install )
    if [ "$(id -u)" -eq 0 ]; then
      ldconfig
    fi
  fi
fi

echo "== Summary =="

OVERRIDES="GZCAT=zcat"
if [ "$KSH_BIN" != "/bin/ksh" ]; then
  OVERRIDES="$OVERRIDES KSH=$KSH_BIN"
fi
if [ "$PREFIX" != "/usr/local" ]; then
  OVERRIDES="$OVERRIDES BSTG_TAP_CFLAGS=\"-I$PREFIX/include\" BSTG_TAP_LDFLAGS=\"-L$PREFIX/lib -ltap\""
fi

echo "To build and test on Linux, run:"
echo "  cd src && $MAKE_CMD $OVERRIDES"

if [ "$PREFIX" != "/usr/local" ]; then
  echo ""
  echo "Note: export LD_LIBRARY_PATH=$PREFIX/lib if running the built binaries directly."
fi

exit 0
