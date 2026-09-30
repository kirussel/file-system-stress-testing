#!/bin/ksh
. ../shlib/bsd.lib

#
# Copyright 2011 Google Inc.
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

#
# Just run one iteration of aibreann
#
aibreann_check()
{
  typeset md
  typeset dsk
  typeset acount

  acount=${2}
  dsk=${1}
  # we expect this file to exist
  if [ ! -s "${dsk}" ] ; then
    return 1
  fi

  if ! ${GZCAT} < "${dsk}" > rd51.dsk ; then
    return 1
  fi
  dsk=rd51.dsk

  # config file to be a md(4) disk
  md="$(bstg_dskvnconfig "${dsk}")"
  if [ -z "${md}" ] ; then
    return 1
  fi

  # mount this filesystem
  if ! bstg_bsdmount "/dev/${md}" "${MNT}" "${dsk}" ; then
    ${SUDO} bstg_undskvnconfig "${md}"
    return 1
  fi

  printf "count=%d\n" "${acount}"
  ./aibreann -f "${acount}"

  # umount this new filesystem
  if ! ${SUDO} umount "${MNT}" ; then
    return 1
  fi

  # do an fsck
  if ! bstg_bsdfsck "${md}" ; then
    return 1
  fi

  if ! bstg_undskvnconfig "${md}" ; then
    return 1
  fi

  return 0
}


#
# Create and initialize a new filesystem suitable for this test case.
#
aibreann_checkall()
{
  typeset dsk
  typeset funcs
  typeset acount

  let acount=0
  funcs="$(./aibreann -n)"
  if [ -z "${funcs}" ] ; then
    return 1
  fi

  while [ ${acount} -lt "${funcs}" ]
  do
    dsk=${1}
    if ! aibreann_check "${dsk}" ${acount} ; then
      return 1
    fi

    let acount=acount+1
  done

  return 0
}

aibreann_checkall "${1}"
exit
