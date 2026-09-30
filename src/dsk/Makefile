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

include ../inc/default.mk

DIR111=$(BASEDIR)/111
ONEMG=1048576

# FreeBSD-only bits of the tree, on by default: the `uchg` flag on the two
# copies of true, and the empty symlink Linux rejects. Callers that build
# the tree under a plain directory (no FreeBSD/UFS underneath, e.g.
# contrib/aibreann-tmp.sh) pass UCHG= and EMPTYSYMLINK= to skip them.
UCHG?=-f uchg
EMPTYSYMLINK?=yes

# rd51/ra81/floppy.dsk.Z is installed into 111/ as tree content; DSK_TREE
# picks which one. Defaults to rd51, the one aibreann-tmp.sh also uses.
DSK_TREE?=rd51

DSK=rd51.dsk ra81.dsk floppy.dsk
RAWDSK=$(addsuffix  ,$(basename $(DSK)))

.PHONY: all clean install tree pathstore

all: true $(DSK:%=%) $(DSK:%=%.gz)

true.c:
	printf "int main() { return 0; }\n" > $@

true.sh:
	printf "#!%s\n" $(DIR111)/true > $@

true: true.c

$(DSK): % : %.Z
	$(GZCAT) < $< > $@

$(RAWDSK:%=%.dsk.gz): %.dsk.gz: %.dsk %.h
	gzip -1 < $< > $@

# Populate $(BASEDIR) with the tree aibreann exercises: dirs 111 through
# fff, assorted regular/special files under 111, and a symlink chain.
# Shared by the %.h recipe below (mounted disk image) and
# contrib/aibreann-tmp.sh (plain directory tree).
tree: true true.sh
	$(MKDIR) -p $(DIR111)
	chmod 0777 $(DIR111)
	$(MKDIR) -m 0777 $(BASEDIR)/222
	$(MKDIR) -m 0777 $(BASEDIR)/333
	$(MKDIR) -m 0777 $(BASEDIR)/444
	$(MKDIR) -m 0777 $(BASEDIR)/555
	$(MKDIR) -m 0777 $(BASEDIR)/666
	$(MKDIR) -m 0777 $(BASEDIR)/777
	$(MKDIR) -m 0777 $(BASEDIR)/888
	$(MKDIR) -m 0777 $(BASEDIR)/999
	$(MKDIR) -m 0777 $(BASEDIR)/aaa
	$(MKDIR) -m 0777 $(BASEDIR)/bbb
	$(MKDIR) -m 0777 $(BASEDIR)/ccc
	$(MKDIR) -m 0777 $(BASEDIR)/ddd
	$(MKDIR) -m 0777 $(BASEDIR)/eee
	$(MKDIR) -m 0777 $(BASEDIR)/fff
	$(INSTALL) -c -s $(UCHG) -m 0777 true $(DIR111)/..
	$(INSTALL) -c -s $(UCHG) -m 0777 true $(DIR111)
	$(INSTALL) -c -s -m 0777 true $(DIR111)/true2
	$(INSTALL) -c -m 0777 true.sh $(DIR111)
	$(INSTALL) -c -m 0777 $(DSK_TREE).dsk.Z $(DIR111)
	$(MKDIR) $(DIR111)/adir
	chmod 0777 $(DIR111)/adir
	mkfifo -m 0777 $(DIR111)/afifo
	truncate -s $(ONEMG) $(DIR111)/ahole
	chmod 0777 $(DIR111)/ahole
	$(INSTALL) -c -s -m 0777 true $(DIR111)/true.truncate
	truncate -s 8 $(DIR111)/true.truncate
	truncate -s $(ONEMG) $(DIR111)/true.truncate
	$(INSTALL) -c -m 0777 true.sh $(DIR111)/true.sh.truncate
	truncate -s 8 $(DIR111)/true.sh.truncate
	truncate -s $(ONEMG) $(DIR111)/true.sh.truncate
	$(if $(EMPTYSYMLINK),umask 0 ; ln -s "" $(DIR111)/symlinkempty)
	umask 0 ; ln -s "a" $(DIR111)/b
	umask 0 ; ln -s "b" $(DIR111)/c
	umask 0 ; ln -s "c" $(DIR111)/a

# Same listing the %.h recipe below generates. PATHSTORE names the output
# file; contrib/aibreann-tmp.sh points it at src/aibreann/pathstore.h.
pathstore:
	find $(DIR111)/* | xargs -n 1 printf "  \"%s\",\n" > "$(PATHSTORE)"

# TODO(k) cleanup the BASEDIR creation

$(RAWDSK:%=%.h): %.h: %.dsk true true.sh
	$(SETENV) GZCAT=$(GZCAT) SUDO=$(SUDO) MNT=$(BASEDIR) $(KSH) ./mkdsk.sh $^
	$(SUDO) chown $(USER) $(BASEDIR)
	$(MAKE) tree DSK_TREE=$*
	$(MAKE) pathstore PATHSTORE=$@
	$(SETENV) GZCAT=$(GZCAT) SUDO=$(SUDO) MNT=$(BASEDIR) $(KSH) ./umdsk.sh

clean:
	$(RM) true true.c true.sh md.txt
	$(RM) $(RAWDSK:%=%.h) $(DSK:%=%.gz) $(DSK)

LINT_SH=mkdsk.sh umdsk.sh
include ../inc/lint.mk
