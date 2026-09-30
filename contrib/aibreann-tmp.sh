#!/bin/sh
#
# Run every aibreann function against a plain directory tree instead of a
# freshly mounted disk image, so aibreann can be exercised on Linux/CI.
#
# This mirrors src/aibreann/check.sh, but instead of mounting a new copy of
# the rd51 image for each function, it rebuilds under $BASEDIR the same tree
# src/dsk/Makefile lays out on that image (minus the FreeBSD-only bits: no
# chflags uchg, and no empty symlink, which Linux rejects).
#
# src/lib and src/funcs must already be built with the same BASEDIR, since
# BSTG_BASEDIR is compiled into them too. Run from the top of the repo:
#
#   BASEDIR=/tmp/aibreann prove -v --exec sh contrib/aibreann-tmp.sh
#
# Output is TAP on stdout; build and aibreann output go to stderr.
#
# The build needs GNU make: set MAKE to pick one (e.g. MAKE=gmake),
# otherwise gmake is used if installed, else make.
#
set -e

: "${BASEDIR:?set BASEDIR to the directory to run aibreann in}"
KSH=${KSH:-bash}
if [ -z "$MAKE" ]; then
  if command -v gmake > /dev/null 2>&1; then
    MAKE="gmake"
  else
    MAKE="make"
  fi
fi
DSK=rd51
DIR111=$BASEDIR/111
ONEMG=1048576

top=$(pwd)
work=$(mktemp -d)
trap 'rm -rf "$work" "$BASEDIR"; rm -fv src/aibreann/pathstore.h >&2' EXIT

printf "int main() { return 0; }\n" > "$work/true.c"
cc -o "$work/true" "$work/true.c"
printf "#!%s\n" "$DIR111/true" > "$work/true.sh"

mktree()
{
  rm -rf "$BASEDIR"
  mkdir -p "$DIR111"
  chmod 0777 "$BASEDIR" "$DIR111"
  for d in 222 333 444 555 666 777 888 999 aaa bbb ccc ddd eee fff; do
    mkdir -m 0777 "$BASEDIR/$d"
  done
  install -c -s -m 0777 "$work/true" "$BASEDIR"
  install -c -s -m 0777 "$work/true" "$DIR111"
  install -c -s -m 0777 "$work/true" "$DIR111/true2"
  install -c -m 0777 "$work/true.sh" "$DIR111"
  install -c -m 0777 "src/dsk/$DSK.dsk.Z" "$DIR111"
  mkdir -m 0777 "$DIR111/adir"
  mkfifo -m 0777 "$DIR111/afifo"
  truncate -s $ONEMG "$DIR111/ahole"
  chmod 0777 "$DIR111/ahole"
  install -c -s -m 0777 "$work/true" "$DIR111/true.truncate"
  truncate -s 8 "$DIR111/true.truncate"
  truncate -s $ONEMG "$DIR111/true.truncate"
  install -c -m 0777 "$work/true.sh" "$DIR111/true.sh.truncate"
  truncate -s 8 "$DIR111/true.sh.truncate"
  truncate -s $ONEMG "$DIR111/true.sh.truncate"
  (umask 0; ln -s a "$DIR111/b"; ln -s b "$DIR111/c"; ln -s c "$DIR111/a")
}

# Same listing src/dsk/Makefile generates for rd51.h.
mktree
find "$DIR111"/* | xargs -n 1 printf "  \"%s\",\n" > src/aibreann/pathstore.h
"$MAKE" -C src/aibreann aibreann -o pathstore.h KSH="$KSH" >&2

# Everything in the tree that a function could change: type, mode, size,
# mtime, link target and file contents.
snap()
{
  find "$BASEDIR" -printf "%p %y %m %s %T@ %l\n" | sort
  find "$BASEDIR" -type f -exec md5sum {} + | awk '{ print $2, "md5", $1 }' |
      sort
}

# Emit TAP: one test per function (it exited 0; the description says how
# many tree entries it changed), plus a final test that the run as a whole
# mutated the tree. Some functions are read-only, so no single function is
# required to change anything.
n=$(src/aibreann/aibreann -n)
echo "1..$((n + 1))"
i=0
failed=0
mutated=0
while [ $i -lt "$n" ]; do
  mktree
  snap > "$work/before"
  rc=0
  (cd "$BASEDIR" && timeout 60 "$top/src/aibreann/aibreann" -f $i) \
      2> "$work/err" || rc=$?
  cat "$work/err" >&2
  name=$(head -n 1 "$work/err")
  snap > "$work/after"
  # Distinct paths that were added, removed or modified.
  changed=$(diff "$work/before" "$work/after" | awk '/^[<>]/ { print $2 }' |
      sort -u | wc -l)
  [ "$changed" -gt 0 ] && mutated=$((mutated + 1))
  if [ $rc -eq 0 ]; then
    echo "ok $((i + 1)) - f=$i $name: changed $changed entries"
  else
    echo "not ok $((i + 1)) - f=$i $name: exited $rc"
    failed=$((failed + 1))
  fi
  i=$((i + 1))
done

if [ $mutated -gt 0 ]; then
  echo "ok $((n + 1)) - $mutated of $n functions mutated $BASEDIR"
else
  echo "not ok $((n + 1)) - no function mutated $BASEDIR"
  failed=$((failed + 1))
fi

[ $failed -eq 0 ]
