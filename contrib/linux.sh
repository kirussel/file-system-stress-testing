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
#
# Builds and installs libtap on Linux -- see AGENTS.md for the rest of the
# Linux setup (compiler/make/prove/ksh, and the make overrides to use once
# this is done).
#
# This repo's unit tests (src/lib/*_test.c, src/funcs/*_test.c) link against
# the original C TAP library by Nik Clayton (plan_tests()/ok()), not the
# more commonly-found zorgnax/libtap fork (plan()/tap_plan()/ok_at_loc()),
# which is API-incompatible and fails to link with
# "undefined reference to 'plan_tests'". This script clones and builds the
# correct one (pozorvlak/libtap) from source.
#
# Install prefix defaults to /usr/local; override with PREFIX=... if you
# don't have root, e.g.:
#   PREFIX=$HOME/.local ./contrib/linux.sh

set -eu

PREFIX="${PREFIX:-/usr/local}"

if [ -f "$PREFIX/include/tap.h" ]; then
  if grep -q "plan_tests" "$PREFIX/include/tap.h"; then
    echo "libtap is already installed under $PREFIX -- nothing to do."
    exit 0
  fi
  echo "Error: $PREFIX/include/tap.h looks like the wrong (zorgnax) libtap fork."
  echo "It uses plan()/tap_plan()/ok_at_loc() instead of plan_tests()/ok()."
  echo "Remove $PREFIX/include/tap.h and $PREFIX/lib/libtap.* before re-running."
  exit 1
fi

for tool in git autoconf automake libtool; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Missing '$tool'. Hint: apt-get install -y git autoconf automake libtool"
    exit 1
  fi
done

# Clone into $TMPDIR (or $HOME/tmp), never /tmp; reuse an existing checkout.
TMP="${TMPDIR:-$HOME/tmp}"
TAP_DIR="$TMP/libtap"

if [ ! -d "$TAP_DIR/.git" ]; then
  rm -rf "$TAP_DIR"
  mkdir -p "$TMP"
  git clone https://github.com/pozorvlak/libtap.git "$TAP_DIR"
else
  echo "Reusing existing libtap clone at $TAP_DIR"
fi

( cd "$TAP_DIR" && sh bootstrap.sh && ./configure --prefix="$PREFIX" && make )

# Only need root/sudo if $PREFIX (or its nearest existing ancestor, since
# $PREFIX itself may not exist yet) isn't writable by us.
CHECK_DIR="$PREFIX"
while [ ! -e "$CHECK_DIR" ]; do
  CHECK_DIR="$(dirname "$CHECK_DIR")"
done

if [ "$(id -u)" -ne 0 ] && [ ! -w "$CHECK_DIR" ]; then
  if ! command -v sudo >/dev/null 2>&1; then
    echo "Error: $PREFIX is not writable and sudo is not available."
    echo "Re-run as a user who can write to $PREFIX, or set PREFIX to a writable location."
    exit 1
  fi
  ( cd "$TAP_DIR" && sudo make install )
  sudo ldconfig
else
  ( cd "$TAP_DIR" && make install )
  if [ "$(id -u)" -eq 0 ]; then
    ldconfig
  fi
fi

echo "libtap installed under $PREFIX."
if [ "$PREFIX" != "/usr/local" ]; then
  echo "Build this repo with: BSTG_TAP_CFLAGS=\"-I$PREFIX/include\" BSTG_TAP_LDFLAGS=\"-L$PREFIX/lib -ltap\""
fi
