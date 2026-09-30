#!/bin/sh
# shellcheck shell=sh
#
# Copyright 2026 Google LLC
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
# Builds/installs libtap (Nik Clayton's plan_tests()/ok() API) for this
# repo's unit tests. PREFIX defaults to /usr/local.

set -e
PREFIX="${PREFIX:-/usr/local}"

if [ -f "$PREFIX/include/tap.h" ] && grep -q plan_tests "$PREFIX/include/tap.h"; then
  echo "libtap already installed under $PREFIX."
  exit 0
fi

mkdir -p /tmp/libtap
git clone https://github.com/pozorvlak/libtap.git /tmp/libtap
cd /tmp/libtap
sh bootstrap.sh
./configure --prefix="$PREFIX"
make
if [ -w "$PREFIX" ]; then
  make install
else
  sudo make install
  sudo ldconfig
fi
