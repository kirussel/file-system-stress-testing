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

include ./inc/default.mk

.PHONY: all clean lint lint-c

DIRS=lib funcs dsk shlib aibreann brannagh clodagh

all:
	$(MAKE) -C lib
	$(MAKE) -C lib check
	$(MAKE) -C funcs
	$(MAKE) -C funcs check
	$(MAKE) -C dsk
	$(MAKE) -C shlib
	$(MAKE) -C aibreann
	$(MAKE) -C brannagh
	$(MAKE) -C clodagh

clean:
	@for d in $(DIRS) ; do \
		$(MAKE) -C $$d $@; \
	done

lint:
	@rc=0; \
	$(SHELLCHECK) etc/prove || rc=1; \
	for d in $(DIRS) ; do \
		$(MAKE) -C $$d $@ || rc=1; \
	done; \
	exit $$rc

# Check C formatting against the repo-root .clang-format; files listed in
# .clang-format-ignore are skipped.
lint-c:
	git ls-files '*.c' '*.h' | xargs $(CLANG_FORMAT) --dry-run --Werror
