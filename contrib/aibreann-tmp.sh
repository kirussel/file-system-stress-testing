#!/bin/sh
#
# Run aibreann functions against a plain directory tree instead of a
# freshly mounted disk image, so aibreann can be exercised on Linux/CI.
#
# This mirrors src/aibreann/check.sh, but instead of mounting a new copy of
# the rd51 image for each function, it rebuilds under $BASEDIR the same tree
# src/dsk/Makefile's `tree` target lays out on that image, with the
# FreeBSD-only bits turned off: no chflags uchg, and no empty symlink,
# which Linux rejects.
#
# src/lib and src/funcs must already be built with the same BASEDIR, since
# BSTG_BASEDIR is compiled into them too. Run from the top of the repo:
#
#   BASEDIR=/tmp/aibreann prove -v --exec sh contrib/aibreann-tmp.sh
#
# Output is TAP on stdout; build and aibreann output go to stderr.
#
# By default only the functions that change the tree are run (see SKIP
# below); set AIBREANN_ALL=1 to run all of them.
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

top=$(pwd)
work=$(mktemp -d)

# src/dsk/true{,.c,.sh} are build outputs of src/dsk/Makefile's `tree`
# target, shared with the normal FreeBSD dsk build. Only clean up true and
# true.c if this run creates them; leave any that already exist alone.
dsktrue_new=
for f in true true.c; do
  if [ ! -e "src/dsk/$f" ] && [ ! -L "src/dsk/$f" ]; then
    dsktrue_new="$dsktrue_new src/dsk/$f"
  fi
done

# true.sh bakes in $BASEDIR, and its make rule has no prerequisites, so an
# existing one (from a /mnt dsk build, or a killed run with a different
# BASEDIR) would be reused as is. Set it aside, regenerate it for every
# tree, and put the original back on exit.
if [ -e src/dsk/true.sh ] || [ -L src/dsk/true.sh ]; then
  mv src/dsk/true.sh "$work/true.sh.saved"
fi
trap 'rm -fv -- $dsktrue_new src/dsk/true.sh src/aibreann/pathstore.h >&2
  if [ -e "$work/true.sh.saved" ] || [ -L "$work/true.sh.saved" ]; then
    mv -v "$work/true.sh.saved" src/dsk/true.sh >&2
  fi
  rm -rf "$work" "$BASEDIR"' EXIT

mktree()
{
  rm -rf "$BASEDIR"
  mkdir -p "$BASEDIR"
  chmod 0777 "$BASEDIR"
  rm -f src/dsk/true.sh
  "$MAKE" -s -C src/dsk tree BASEDIR="$BASEDIR" DSK_TREE="$DSK" UCHG= \
      EMPTYSYMLINK= >&2
}

# Same listing src/dsk/Makefile's pathstore target generates for rd51.h.
mktree
"$MAKE" -s -C src/dsk pathstore BASEDIR="$BASEDIR" \
    PATHSTORE="$top/src/aibreann/pathstore.h" >&2
"$MAKE" -C src/aibreann aibreann -o pathstore.h KSH="$KSH" >&2

# Functions that leave the tree unchanged when run on their own (they only
# read, or need state such as an open fd or a free path that a lone run
# doesn't provide), so running them only costs time. closeall is never
# called by aibreann. Set AIBREANN_ALL=1 to run every function anyway.
SKIP="
special_exec special_gcore special_ldlibrarypath special_ldpreload
special_posix_spawn src_access_access src_access_eaccess src_chdir_chdir
src_chdir_fchdir src_chown_fchown src_close_closeall src_devname_devname
src_dlopen_dlopen src_link_link src_mkdir_mkdir src_mkfifo_mkfifo
src_mmap_ar src_mmap_cp src_mmap_file src_open_append src_open_osync
src_open_rdwr src_opendir_fdopendir src_opendir_opendir src_opendir_readdir
src_pathconf_fpathconf src_pathconf_lpathconf src_pathconf_pathconf
src_pipe_pipe src_read_read src_read_readv src_readlink_readlink
src_realpath_realpath src_rename_rename src_sendfile_sendfile
src_statvfs_fstatvfs src_statvfs_statvfs src_symlink_bigsymlink
src_symlink_emptysymlink src_symlink_symlink src_sync_fsync src_sync_sync
src_ttyname_ttyname src_umount_umount
"

# Everything in the tree that a function could change: type, mode, size,
# mtime, link target and file contents. Perl rather than find -printf and
# md5sum, which FreeBSD lacks; prove needs perl anyway.
snap()
{
  perl -MFile::Find -MFcntl=:mode -MDigest::MD5 -MTime::HiRes=lstat -e '
    find({ no_chdir => 1, wanted => sub {
      my @s = lstat($_) or return;
      my $m = $s[2];
      my $t = S_ISLNK($m) ? "l" : S_ISDIR($m) ? "d" : S_ISREG($m) ? "f" :
          S_ISFIFO($m) ? "p" : "o";
      my $x = "";
      if ($t eq "l") {
        $x = readlink($_);
      } elsif ($t eq "f") {
        my $fh;
        $x = open($fh, "<", $_) ?
            Digest::MD5->new->addfile($fh)->hexdigest : "unreadable";
      }
      printf "%s %s %o %d %s %s\n", $_, $t, $m & 07777, $s[7], $s[9], $x;
    } }, $ARGV[0]);
  ' "$BASEDIR" | sort
}

# Map function names to their aibreann -f index, from the generated table.
sed -n 's/^ *\([a-z0-9_]*\)_bstg_funcs, \/\* \([0-9]*\) \*\/$/\2 \1/p' \
    src/funcs/fembotfuncs.h > "$work/funcs"
n=$(src/aibreann/aibreann -n)
if [ "$(awk 'END { print NR }' "$work/funcs")" -ne "$n" ]; then
  echo "cannot map src/funcs/fembotfuncs.h to $n aibreann functions" >&2
  exit 1
fi
if [ "${AIBREANN_ALL:-0}" = 1 ]; then
  cp "$work/funcs" "$work/run"
else
  echo "$SKIP" | tr ' ' '\n' | grep . > "$work/skip"
  awk 'NR == FNR { skip[$1]; next } !($2 in skip)' \
      "$work/skip" "$work/funcs" > "$work/run"
fi
nrun=$(awk 'END { print NR }' "$work/run")

# Emit TAP: one test per function run (it exited 0; the description says
# how many tree entries it changed), plus a final test that the run as a
# whole mutated the tree.
echo "1..$((nrun + 1))"
t=0
failed=0
mutated=0
while read -r i name; do
  t=$((t + 1))
  mktree
  snap > "$work/before"
  rc=0
  (cd "$BASEDIR" && timeout 60 "$top/src/aibreann/aibreann" -f "$i") \
      2> "$work/err" < /dev/null || rc=$?
  cat "$work/err" >&2
  snap > "$work/after"
  # Distinct paths that were added, removed or modified.
  changed=$(diff "$work/before" "$work/after" | awk '/^[<>]/ { print $2 }' |
      sort -u | awk 'END { print NR }')
  if [ "$changed" -gt 0 ]; then
    mutated=$((mutated + 1))
  fi
  if [ $rc -eq 0 ]; then
    echo "ok $t - f=$i $name: changed $changed entries"
  else
    echo "not ok $t - f=$i $name: exited $rc"
    failed=$((failed + 1))
  fi
done < "$work/run"

t=$((t + 1))
if [ $mutated -gt 0 ]; then
  echo "ok $t - $mutated of $nrun functions mutated $BASEDIR"
else
  echo "not ok $t - no function mutated $BASEDIR"
  failed=$((failed + 1))
fi

[ $failed -eq 0 ]
