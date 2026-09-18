CC ?= cc
# Read out of the header rather than written here, so there is one place a
# version is said. See D983.
KEST_VERSION := $(shell grep KEST_VERSION_STRING include/kest.h | cut -d'"' -f2)
WARN := -std=c11 -Wall -Wextra -Wshadow -Wconversion -Werror

# The hosts are held to everything the library is except shadowing. Each is one
# long `main` of blocks run one after another, and a block that declares `said`
# where the block above it declared `said` is two names for two things in two
# places rather than one name for two things in one. In the library a shadowed
# name is what `sscanf(at, ...)` reading the wrong `at` looks like, so there it
# is an error. See D973.
HOSTWARN := -std=c11 -Wall -Wextra -Werror

# Where the standard library ends up, which the compiler has to be able to
# find when nothing else says where it is.
PREFIX ?= /usr/local
DESTDIR ?=
SRC := $(filter-out src/main.c,$(wildcard src/*.c))

RELEASE_OBJ := $(SRC:src/%.c=build/release/%.o)
DEBUG_OBJ := $(SRC:src/%.c=build/debug/%.o)

# The language is a library first: `kest` is one host of it, and the example
# beside it is another.
kest: build/release/main.o libkest.a
	$(CC) -o $@ $^ -lm

libkest.a: $(RELEASE_OBJ)
	ar rcs $@ $^

build/release/%.o: src/%.c | build/release
	$(CC) $(WARN) -O2 -Iinclude -DKEST_LIB_DIR='"$(PREFIX)/lib/kest/"' -MMD -MP -c -o $@ $<

# Bytes the compiler was not written for, made from a seed. Built both ways:
# the release one for a long campaign and the sanitised one for the gate's
# short one, because a read past the end of something is a report in the
# second and whatever was next in the first. See D984.
tools/fuzz: tools/fuzz.c libkest.a include/kest.h
	$(CC) $(WARN) -O2 -Iinclude -o $@ tools/fuzz.c libkest.a -lm

tools/fuzz-debug: tools/fuzz.c $(DEBUG_OBJ) include/kest.h
	$(CC) $(HOSTWARN) -O0 -g -fsanitize=address,undefined -Iinclude -o $@ \
		tools/fuzz.c $(DEBUG_OBJ) -lm

kest-debug: build/debug/main.o $(DEBUG_OBJ)
	$(CC) -fsanitize=address,undefined -o $@ $^ -lm

build/debug/%.o: src/%.c | build/debug
	$(CC) $(WARN) -O0 -g -fsanitize=address,undefined -Iinclude -DKEST_LIB_DIR='"$(PREFIX)/lib/kest/"' -MMD -MP -c -o $@ $<

build/release build/debug:
	mkdir -p $@

# A host that is not this command line. It is compiled to an object of its own
# rather than straight to a binary, because what a host calls is readable in an
# object and gone once it is linked, and holding the public header to being
# used is holding it to what these two call.
build/release/embed.o: examples/embed.c include/kest.h | build/release
	$(CC) $(HOSTWARN) -O2 -Iinclude -MMD -MP -c -o $@ $<

examples/embed: build/release/embed.o libkest.a
	$(CC) -o $@ $^

# The same host under the sanitisers. It is the only thing that crosses the
# public boundary in both directions, so it is the only thing that can say
# whether lending memory is right.
examples/embed-debug: examples/embed.c $(DEBUG_OBJ)
	$(CC) $(HOSTWARN) -O0 -g -fsanitize=address,undefined -Iinclude -o $@ $^

# The engine: a host in the shape a host has, built both ways. It drives a
# world a frame at a time and reloads the program under it, which is the one
# thing no other host here does.
build/release/engine.o: examples/engine.c include/kest.h | build/release
	$(CC) $(HOSTWARN) -O2 -Iinclude -MMD -MP -c -o $@ $<

examples/engine: build/release/engine.o libkest.a
	$(CC) -o $@ $^

examples/engine-debug: examples/engine.c $(DEBUG_OBJ)
	$(CC) $(HOSTWARN) -O0 -g -fsanitize=address,undefined -Iinclude -o $@ $^

# The smallest host there is, built the same way: a host writer reads it, and a
# host nobody builds is a host that stops working without saying so.
build/release/least.o: examples/least.c include/kest.h | build/release
	$(CC) $(WARN) -O2 -Iinclude -MMD -MP -c -o $@ $<

examples/least: build/release/least.o libkest.a
	$(CC) -o $@ $^

debug: kest-debug
least: examples/least
embed: examples/embed
embed-debug: examples/embed-debug
engine: examples/engine
engine-debug: examples/engine-debug

# What a change is tried against while it is being written: the build, the
# examples, the library, the one form, a diagnostic and the other host. Seconds.
fast: tools/fast.sh
	@tools/fast.sh

# Everything, so that "it passes" is a command rather than a claim. Minutes.
# Run at a milestone and before saying something is done, not after every edit.
check: tools/check.sh
	@tools/check.sh

# One number: how long a frame step takes per entity. Not part of `check`,
# because a duration is not a pass or a fail, and written down nowhere.
time: kest tools/inward
	@./kest run tools/frame.kest
	@./kest run tools/crossing.kest
	@./kest run tools/reference.kest
	@./tools/inward

# The other direction, which a program cannot measure about itself: a host is
# what calls in, so the thing that measures a call in is a host.
tools/inward: tools/inward.c libkest.a include/kest.h
	$(CC) $(WARN) -O2 -Iinclude -o $@ tools/inward.c libkest.a -lm

# Where another project looks.
install: kest libkest.a
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(PREFIX)/include
	mkdir -p $(DESTDIR)$(PREFIX)/lib/kest/std
	cp kest $(DESTDIR)$(PREFIX)/bin/kest
	cp include/kest.h $(DESTDIR)$(PREFIX)/include/kest.h
	cp libkest.a $(DESTDIR)$(PREFIX)/lib/libkest.a
	cp lib/std/*.kest $(DESTDIR)$(PREFIX)/lib/kest/std/

# A release, which is an install into a directory of its own plus the source a
# host vendors and the extension an editor wants, in one archive with its
# checksum beside it. There is no artifact from the compiler in it: the
# bytecode is not a format (D983), so what ships is the command line, the
# library, the header, the standard library and the source. See D989.
RELEASE := kest-$(KEST_VERSION)-$(shell uname -s | tr A-Z a-z)-$(shell uname -m)

release: kest libkest.a
	rm -rf build/$(RELEASE)
	$(MAKE) install DESTDIR=build/$(RELEASE) PREFIX=
	mkdir -p build/$(RELEASE)/src build/$(RELEASE)/editors
	cp src/*.c src/*.h build/$(RELEASE)/src/
	cp -r editors/vscode build/$(RELEASE)/editors/
	cp README.md CHANGELOG.md build/$(RELEASE)/
	cp -r docs build/$(RELEASE)/docs
	cd build && tar czf $(RELEASE).tar.gz $(RELEASE)
	cd build && sha256sum $(RELEASE).tar.gz > $(RELEASE).tar.gz.sha256
	@echo "wrote build/$(RELEASE).tar.gz and its checksum"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/kest
	rm -f $(DESTDIR)$(PREFIX)/include/kest.h
	rm -f $(DESTDIR)$(PREFIX)/lib/libkest.a
	rm -rf $(DESTDIR)$(PREFIX)/lib/kest

clean:
	rm -rf build kest kest-debug libkest.a examples/embed \
	    examples/embed-debug examples/engine examples/engine-debug \
	    examples/least tools/inward tools/fuzz tools/fuzz-debug

.PHONY: debug least embed embed-debug engine engine-debug fast check time \
    fuzz release install uninstall clean

# A short campaign, which is what a gate can afford: eight seeds and four
# hundred inputs each, sanitised. A longer one is the same command with other
# numbers, and what a finding is is a seed and a count.
fuzz: tools/fuzz-debug
	@for what in source handles lends refs text migrate; do \
	    for seed in 1 2 3 4 5 6 7 8; do \
	        ./tools/fuzz-debug $$seed 400 build/fuzz.kest $$what || exit 1; \
	    done; \
	done

-include $(RELEASE_OBJ:.o=.d) $(DEBUG_OBJ:.o=.d) build/release/main.d \
    build/debug/main.d build/release/embed.d build/release/engine.d \
    build/release/least.d
