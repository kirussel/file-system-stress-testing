# Agent Guide

This repository is a monkey-test framework for stress testing POSIX
filesystems. All source code lives under `src/`; build it with GNU make from
that directory (`cd src && make`).

## Setting up a development environment

### 1. Prerequisites

- A C compiler (`gcc` or `clang`)
- GNU make
- `git`
- `prove` (ships with Perl) to run the TAP unit tests

### 2. Install `libtap`

`libtap` is a C testing framework. Since it is often not packaged in default
Linux repositories, the most reliable way to install it is from source:

```sh
# Clone a standard libtap implementation
git clone https://github.com/zorgnax/libtap.git /tmp/libtap
cd /tmp/libtap

# Build and install it globally
make
sudo make install

# This will install:
#   /usr/local/include/tap.h
#   /usr/local/lib/libtap.so (or libtap.a)

# Update the dynamic linker cache so the tests can find libtap.so
sudo ldconfig
```

The unit tests (`src/lib/*_test.c`, `src/funcs/*_test.c`) include `tap.h` and
link with `-ltap`. `src/inc/tap.mk` looks for them under `/usr/local` by
default. If you installed libtap somewhere else, override the paths:

```sh
make BSTG_TAP_CFLAGS=-I/opt/libtap/include BSTG_TAP_LDFLAGS="-L/opt/libtap/lib -ltap"
```
