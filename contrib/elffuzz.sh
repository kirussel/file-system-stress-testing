#
# Copyright 2009, Google Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
# * Neither the name of Google Inc. nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#
# Create a small ELF executable as a base.
#
rm -f true.c a.out
printf "int main() { return 0; }\n" > true.c
cc -o a.out true.c

#
# Loop forever.
# Create a corrupted ELF exeutable.
# Run this executable.
#
# https://raw.githubusercontent.com/sughodke/fsfuzzer/master/mangle.c
#
typeset -i acount
let acount=0
while true
do
  #
  # setup working directory
  #
  let acount=acount+1
  DIRNAME=f${acount}s"$(date +'%s')"
  echo $DIRNAME
  mkdir $DIRNAME
  cd $DIRNAME

  #
  # setup this ELF image
  #
  cp -f ../a.out ./curr.img 
  mangle curr.img "$( wc -c < ./curr.img)"
  md5 curr.img > curr.img.md5
  sync ; sync ; sync

  #
  # Run corrupted executable in a sub-shell.
  # Prevent the generation of core files.
  # Limit run time, in case executable has endless loop.
  #
  (
    ulimit -c 0
    ulimit -t 1
    exec ./curr.img
  ) > /dev/null 2> /dev/null < /dev/null
  cd ..

  #
  # kernel did not crash, try again
  #
  rm -rf $DIRNAME
done
