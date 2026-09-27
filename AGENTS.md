# Agent Guide

This repository is a monkey-test framework for stress testing POSIX
filesystems. All source code lives under `src/`; build it with GNU make from
that directory (`cd src && make`). It was written for FreeBSD, so building
and testing it on Linux needs a handful of workarounds — they're all
documented below.

## Setting up a development environment

### 1. Prerequisites

- A C compiler (`gcc` or `clang`)
- GNU make
- `git`
- `prove` (ships with Perl) to run the TAP unit tests
- `ksh` — the code generators under `src/funcs` (`mkfuncs.sh`, `mkproto.sh`,
  `mktable.sh`) are written in Korn shell, not `/bin/sh`. On Debian/Ubuntu:

  ```sh
  sudo apt-get install -y ksh
  ```

  Without it, `make -C funcs` fails with
  `env: '/bin/ksh': No such file or directory`.

### 2. Install `libtap`

The unit tests (`src/lib/*_test.c`, `src/funcs/*_test.c`) `#include "tap.h"`
and call `plan_tests()` / `ok()`. That is the API of the **original C TAP
library written by Nik Clayton**, not the API of the more commonly-found
`zorgnax/libtap` fork (which uses `plan()`/`tap_plan()`/`ok_at_loc()`
instead and will fail to link with `undefined reference to 'plan_tests'`).

Build and install the compatible implementation from source:

```sh
# Clone the Nik Clayton libtap implementation (autotools-based)
git clone https://github.com/pozorvlak/libtap.git /tmp/libtap
cd /tmp/libtap

# Needs autoconf/automake/libtool
sudo apt-get install -y autoconf automake libtool

sh bootstrap.sh
./configure
make
sudo make install

# This installs:
#   /usr/local/include/tap.h
#   /usr/local/lib/libtap.so / libtap.a

# Update the dynamic linker cache so the tests can find libtap.so
sudo ldconfig
```

If you already have a `/usr/local/include/tap.h` from the wrong (zorgnax)
libtap, remove it first (`sudo rm /usr/local/include/tap.h
/usr/local/lib/libtap.*`) before installing the correct one, so headers and
libraries don't get mixed.

`src/inc/tap.mk` looks for `tap.h`/`libtap` under `/usr/local` by default.
If you installed libtap somewhere else, override the paths:

```sh
make BSTG_TAP_CFLAGS=-I/opt/libtap/include BSTG_TAP_LDFLAGS="-L/opt/libtap/lib -ltap"
```

### 3. Running the tests: work around missing `gzcat`

The actual TAP unit test suite lives under `src/lib` and `src/funcs` and is
exercised by the `check` target, which the top-level `all` target runs
along the way. But `src/dsk/Makefile` (built earlier in `all`) decompresses
vendored `.dsk.Z` disk images with the `$(GZCAT)` make variable, which
defaults to `gzcat` (`src/inc/default.mk`). Linux ships `gzip`/`zcat` but
not `gzcat`, so a plain `make` fails with `gzcat: not found` before it ever
gets to `dsk`'s later, unrelated failures.

Don't edit the Makefile — override the variable on the command line with a
single-word command (a multi-word value like `"gzip -dc"` breaks the `env
VAR=... cmd` invocation used downstream in `mkdsk.sh`):

```sh
cd src
make GZCAT=zcat
```

This gets you through `make -C lib`, `make -C lib check`, `make -C funcs`,
and `make -C funcs check` — i.e. the actual test suite — before hitting the
disk/mount tooling gap described in item 5 below. `lib` and `funcs`'s
`make check` should report `Result: PASS` for all tests (currently 90 tests
in `lib`, 1 in `funcs`).

### 4. `setproctitle` doesn't exist on Linux

The generated function wrappers in `src/funcs` call `setproctitle(3)`,
which is BSD-only — glibc has no equivalent, so linking fails with
`undefined reference to 'setproctitle'`. `src/funcs/internalfuncs.h`
already has an `#ifdef linux` compatibility block (it patches
`lpathconf`, `S_ISTXT`, etc.); that file now also defines a no-op stub
there:

```c
static inline void setproctitle(const char *fmt, ...) {}
```

If you ever regenerate/replace that file, keep this stub — otherwise
`make -C funcs check` fails to link.

### 5. Building the disk-image tools (`dsk`, `aibreann`, `brannagh`, `clodagh`) needs real FreeBSD tooling

`make all` (beyond `lib` and `funcs`) goes on to build the actual
stress-test *tools*, which need a mounted scratch filesystem to run
against. `src/dsk/mkdsk.sh` calls `bstg_dskvnconfig` / `bstg_newfs` /
`bstg_bsdmount` (see `src/shlib/bsd.lib`), which wrap FreeBSD-only
utilities (`mdconfig`, `newfs`, BSD `mount` semantics). These do not exist
on Linux and there is no package to install for them — porting this layer
means reimplementing it with Linux loopback devices (`losetup`), `mkfs`,
and `mount`, which nothing in this repo does today. Treat this as a
known gap rather than something a missing package will fix.
