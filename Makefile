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
# And a third, built to watch threads rather than memory. The two sanitisers
# cannot be in one binary, and what this one is for is the only thing about
# this library that is about threads at all: two machines of one build, which
# share the build and one count in the process and nothing else. See D1053.
RACES_OBJ := $(SRC:src/%.c=build/races/%.o)

# The language is a library first: `kest` is one host of it, and the example
# beside it is another.
kest: build/release/main.o libkest.a
	$(CC) -o $@ $^ -lm

libkest.a: $(RELEASE_OBJ)
	ar rcs $@ $^

build/release/%.o: src/%.c | build/release
	$(CC) $(WARN) -O2 $(TUNED) -Iinclude -DKEST_LIB_DIR='"$(PREFIX)/lib/kest/"' -MMD -MP -c -o $@ $<

# The machine's loop is one `switch` over every instruction, and how fast it
# runs turned out to hang on where the compiler happened to put each case: a
# change that added five cases, none of which a workload ran, made that
# workload a third slower in cycles at the same instructions, and it came back
# when every case began at a sixteen-byte boundary. So the file the loop is in
# is built with its cases put there, by a compiler that can be asked to; one
# that cannot is asked nothing rather than refused. See D1158.
ALIGNED := $(shell $(CC) -falign-labels=16 -Werror -x c -c -o /dev/null /dev/null 2>/dev/null && echo -falign-labels=16)

# Where the compiler marks every place an indirect jump may land, which it
# does by default on some systems, each of the loop's labels begins with an
# instruction that does nothing unless the processor is told to check jumps
# -- which no Linux tells a program to -- and the machine ran that instruction
# once for every instruction it ran: 2 to 4% of what it retires. So the loop
# is built with returns guarded and jumps not, by a compiler that can be asked
# to. See D1191.
UNMARKED := $(shell $(CC) -fcf-protection=return -Werror -x c -c -o /dev/null /dev/null 2>/dev/null && echo -fcf-protection=return)
build/release/vm.o: TUNED := $(ALIGNED) $(UNMARKED)

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

build/races/%.o: src/%.c | build/races
	$(CC) $(WARN) -O1 -g -fsanitize=thread -Iinclude -DKEST_LIB_DIR='"$(PREFIX)/lib/kest/"' -MMD -MP -c -o $@ $<

build/release build/debug build/races:
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
races: $(RACES_OBJ)
least: examples/least
embed: examples/embed
embed-debug: examples/embed-debug
engine: examples/engine
engine-debug: examples/engine-debug

# What a change is tried against while it is being written: the build, the
# examples, the library, the one form, a diagnostic and the other host. Seconds.
fast: tools/fast.sh
	@tools/fast.sh

# Everything but the sweep that puts every check out of order, which is most
# of what the whole gate costs and the one check in it that is about the other
# checks rather than about this language. The tier between a tenth of a second
# and half an hour: run it while something is being written, and `check`
# before saying it is done. See D1136.
most: tools/check.sh
	@KEST_HOLES=no tools/check.sh

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

# What a program costs, told apart rather than added up: compiling, starting,
# the first call and the call after that, with the tails and the worst rather
# than the best of five. Not part of `check` either.
bench/measure: bench/measure.c libkest.a include/kest.h
	$(CC) $(WARN) -O2 -Iinclude -o $@ bench/measure.c libkest.a -lm

# The frame workload's host, which owns the bodies and lends them: what a
# frame costs when the data is the host's and crosses once, against what it
# costs when every value crosses on its own, against the same arithmetic in C.
# It is linked with what the other backend wrote for the same program, so
# that the frame it times has two answers: the machine's, and the one a game
# would ship. The generated file is not held to this project's warnings --
# nobody writes it and nobody reads it for style. See D1123.
build/frame-native.c: bench/frame.kest kest | build/release
	./kest emit --c bench/frame.kest > $@

build/frame-native.o: build/frame-native.c include/kest.h
	$(CC) -O2 -Iinclude -DKEST_NO_MAIN -c -o $@ build/frame-native.c

bench/frame: bench/frame.c bench/frame.kest build/frame-native.o libkest.a \
	    include/kest.h
	$(CC) $(WARN) -O2 -Iinclude -o $@ bench/frame.c build/frame-native.o \
	    libkest.a -lm

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

# And what writes the checksum, which is not the same program everywhere:
# `sha256sum` is the GNU one and is what Linux has, `shasum -a 256` is what
# macOS has, and the two write the same two fields in the same order. Found by
# asking rather than by naming a platform, so a machine with both gets the
# first and a machine with neither says so when the release is made rather
# than after it. See D1020.
SHA256 := $(shell command -v sha256sum >/dev/null 2>&1 && echo sha256sum || \
    (command -v shasum >/dev/null 2>&1 && echo "shasum -a 256"))

# What ships. The sources go in beside the library because the runtime is
# vendorable: a host that would rather build it than link it copies `src` and
# `include` and has the whole of it. `VERSION` is written by asking the binary
# in the archive rather than by repeating a number here, so an archive cannot
# say it is something the thing inside it is not. See D998 and D1000.
release: kest libkest.a
	rm -rf build/$(RELEASE)
	$(MAKE) install DESTDIR=build/$(RELEASE) PREFIX=
	mkdir -p build/$(RELEASE)/src build/$(RELEASE)/editors
	cp src/*.c src/*.h build/$(RELEASE)/src/
	cp -r editors/vscode build/$(RELEASE)/editors/
	cp README.md CHANGELOG.md LICENSE build/$(RELEASE)/
	cp -r docs build/$(RELEASE)/docs
	./kest --version > build/$(RELEASE)/VERSION
	@echo "$(shell uname -s | tr A-Z a-z)-$(shell uname -m)" \
	    >> build/$(RELEASE)/VERSION
	@echo "unpack it anywhere; bin/kest finds lib/kest beside it" \
	    >> build/$(RELEASE)/VERSION
	@echo "what it is and how to build a host against it: README.md" \
	    >> build/$(RELEASE)/VERSION
	@echo "what changed: CHANGELOG.md. what it means: docs/language.md" \
	    >> build/$(RELEASE)/VERSION
	@echo "the licence every file of it is under: LICENSE" \
	    >> build/$(RELEASE)/VERSION
	cd build && tar czf $(RELEASE).tar.gz $(RELEASE)
	@test -n "$(SHA256)" || \
	    (echo "no sha256sum and no shasum: nothing here can write a \
checksum" >&2; false)
	cd build && $(SHA256) $(RELEASE).tar.gz > $(RELEASE).tar.gz.sha256
	@echo "wrote build/$(RELEASE).tar.gz and its checksum"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/kest
	rm -f $(DESTDIR)$(PREFIX)/include/kest.h
	rm -f $(DESTDIR)$(PREFIX)/lib/libkest.a
	rm -rf $(DESTDIR)$(PREFIX)/lib/kest

# Everything a build of this tree leaves behind, which is also the list the
# gate reads to know what in the tree a compiler made on purpose. Two lists of
# the same names would be one list the day somebody added to the other, so
# there is one. The comparators `bench/run.sh` builds are here for that reason:
# nothing took them away before, and nothing looked. See D999.
clean:
	rm -rf build kest kest-debug libkest.a examples/embed \
	    examples/embed-debug examples/engine examples/engine-debug \
	    examples/least tools/inward tools/fuzz tools/fuzz-debug \
	    bench/measure bench/frame \
	    bench/control-cpp bench/graph-cpp bench/kernel-cpp bench/words-cpp \
	    bench/rules-cpp \
	    .jitted_scripts kest-colony-day.txt

.PHONY: debug least embed embed-debug engine engine-debug fast most check \
    time fuzz release install uninstall clean

# A short campaign, which is what a gate can afford: eight seeds and four
# hundred inputs each, sanitised. A longer one is the same command with other
# numbers, and what a finding is is a seed and a count.
fuzz: tools/fuzz-debug
	@for what in source handles lends refs text migrate; do \
	    for seed in 1 2 3 4 5 6 7 8; do \
	        ./tools/fuzz-debug $$seed 400 build/fuzz.kest $$what || exit 1; \
	    done; \
	done

# And the thread sanitiser's objects with them. They were left out, so a
# header that changed rebuilt the release and the sanitised builds and left
# these as they were: half the objects held the old shape of a struct and half
# the new, which is a null pointer in the middle of a compile and a gate that
# fails for a reason nothing in the source explains. See D1089.
-include $(RELEASE_OBJ:.o=.d) $(DEBUG_OBJ:.o=.d) $(RACES_OBJ:.o=.d) \
    build/release/main.d \
    build/debug/main.d build/release/embed.d build/release/engine.d \
    build/release/least.d
