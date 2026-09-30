# Agent Guide

This repository is a monkey-test framework for stress testing POSIX
filesystems. All source code lives under `src/`; build it with GNU make
(`gmake`) from that directory. It was written for FreeBSD.

If you're an agent running on Linux: try `contrib/tap.sh` to build/install
the `libtap` this repo's unit tests need, and pass `KSH=bash` to `gmake` to
work around a missing `ksh` (the `src/funcs` code generators have `#!/bin/
ksh` shebangs, but run fine under bash):

```sh
contrib/tap.sh
cd src && gmake KSH=bash
```

To lint the shell scripts with `shellcheck`, run `gmake lint` from `src/`.
