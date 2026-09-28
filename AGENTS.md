# Agent Guide

This repository is a monkey-test framework for stress testing POSIX
filesystems. All source code lives under `src/`; build it with GNU make from
that directory (`cd src && make`). It was written for FreeBSD, so building
and testing it on Linux needs a handful of workarounds.

## Setting up a development environment on Linux

### 1. Prerequisites

- A C compiler (`gcc` or `clang`)
- GNU make (`gmake`, or `make` if it's GNU make)
- `git`
- `prove` (ships with Perl) to run the TAP unit tests
- a Korn shell — the code generators under `src/funcs` (`mkfuncs.sh`,
  `mkproto.sh`, `mktable.sh`) are written in ksh, not `/bin/sh`. Debian/
  Ubuntu's `ksh` package installs to `/bin/ksh`; other distros (e.g. this
  container) may only have `ksh93` elsewhere on `PATH` — see the `KSH=`
  override below.

  ```sh
  sudo apt-get install -y gcc make git perl ksh
  ```

### 2. Build `libtap`

Run `contrib/linux.sh`. It builds and installs the correct `libtap` (the
original C TAP library by Nik Clayton, from `pozorvlak/libtap` — *not* the
`zorgnax/libtap` fork, which has an incompatible `plan()`/`tap_plan()`/
`ok_at_loc()` API instead of `plan_tests()`/`ok()`) if it isn't already
present under `/usr/local`. Override the install location with
`PREFIX=... contrib/linux.sh` if you don't have root.

### 3. `make` variable overrides needed on Linux

- **`GZCAT=zcat`** — `src/dsk/Makefile` (built earlier in `all`)
  decompresses vendored `.dsk.Z` disk images with the `$(GZCAT)` make
  variable, which defaults to `gzcat` (`src/inc/default.mk`). Linux ships
  `gzip`/`zcat` but not `gzcat`, so a plain `make` fails with `gzcat: not
  found` before it ever gets to `dsk`'s later, unrelated failures. Don't
  edit the Makefile — override on the command line with a single-word
  command (a multi-word value like `"gzip -dc"` breaks the `env VAR=...
  cmd` invocation used downstream in `mkdsk.sh`).

- **`KSH=/path/to/ksh`** — the code generators under `src/funcs`
  (`mkfuncs.sh`, `mkproto.sh`, `mktable.sh`) are invoked via the `$(KSH)`
  make variable, which defaults to `/bin/ksh` (`src/inc/default.mk`).
  Distros that only ship `ksh93` (e.g. under `/usr/local/bin` or
  `/usr/bin`) rather than `/bin/ksh` need this override, or `make -C
  funcs` fails with `env: '/bin/ksh': No such file or directory`. Find
  yours with `command -v ksh93 || command -v ksh`.

- **`BSTG_TAP_CFLAGS`/`BSTG_TAP_LDFLAGS`** — only needed if you installed
  `libtap` somewhere other than `/usr/local` (e.g. via `PREFIX=...
  contrib/linux.sh`):

  ```sh
  make BSTG_TAP_CFLAGS=-I$PREFIX/include BSTG_TAP_LDFLAGS="-L$PREFIX/lib -ltap"
  ```

Putting it together, a typical Linux invocation looks like:

```sh
cd src
make GZCAT=zcat KSH=/usr/local/bin/ksh93
```

This gets you through `make -C lib`, `make -C lib check`, `make -C funcs`,
and `make -C funcs check` — i.e. the actual test suite — before hitting the
disk/mount tooling gap described below. `lib` and `funcs`'s `make check`
should report `Result: PASS` for all tests (currently 90 tests in `lib`, 1
in `funcs`).

### Known gaps on Linux

- **`setproctitle`**: the generated function wrappers in `src/funcs` call
  `setproctitle(3)`, which is BSD-only. `src/funcs/internalfuncs.h` has an
  `#ifdef linux` block with a no-op stub for it. If you ever
  regenerate/replace that file, keep the stub — otherwise `make -C funcs
  check` fails to link.
- **Disk-image tools** (`dsk`, `aibreann`, `brannagh`, `clodagh`): `make all`
  (beyond `lib` and `funcs`) goes on to build the actual stress-test
  *tools*, which need a mounted scratch filesystem to run against.
  `src/dsk/mkdsk.sh` calls `bstg_dskvnconfig` / `bstg_newfs` /
  `bstg_bsdmount` (see `src/shlib/bsd.lib`), which wrap FreeBSD-only
  utilities (`mdconfig`, `newfs`, BSD `mount` semantics). These do not exist
  on Linux and there is no package to install for them — porting this layer
  means reimplementing it with Linux loopback devices (`losetup`), `mkfs`,
  and `mount`, which nothing in this repo does today. Treat this as a known
  gap rather than something a missing package will fix.
