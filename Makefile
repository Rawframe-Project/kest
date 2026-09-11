CC ?= cc
WARN := -std=c11 -Wall -Wextra -Werror

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
	$(CC) $(WARN) -O2 -Iinclude -MMD -MP -c -o $@ $<

examples/embed: build/release/embed.o libkest.a
	$(CC) -o $@ $^

# The same host under the sanitisers. It is the only thing that crosses the
# public boundary in both directions, so it is the only thing that can say
# whether lending memory is right.
examples/embed-debug: examples/embed.c $(DEBUG_OBJ)
	$(CC) $(WARN) -O0 -g -fsanitize=address,undefined -Iinclude -o $@ $^

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

# Everything, so that "it passes" is a command rather than a claim.
check: tools/check.sh
	@tools/check.sh

# One number: how long a frame step takes per entity. Not part of `check`,
# because a duration is not a pass or a fail, and written down nowhere.
time: kest
	@./kest run tools/frame.kest

# Where another project looks.
install: kest libkest.a
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)$(PREFIX)/include
	mkdir -p $(DESTDIR)$(PREFIX)/lib/kest/std
	cp kest $(DESTDIR)$(PREFIX)/bin/kest
	cp include/kest.h $(DESTDIR)$(PREFIX)/include/kest.h
	cp libkest.a $(DESTDIR)$(PREFIX)/lib/libkest.a
	cp lib/std/*.kest $(DESTDIR)$(PREFIX)/lib/kest/std/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/kest
	rm -f $(DESTDIR)$(PREFIX)/include/kest.h
	rm -f $(DESTDIR)$(PREFIX)/lib/libkest.a
	rm -rf $(DESTDIR)$(PREFIX)/lib/kest

clean:
	rm -rf build kest kest-debug libkest.a examples/embed \
	    examples/embed-debug examples/least

.PHONY: debug least embed embed-debug check time install uninstall clean

-include $(RELEASE_OBJ:.o=.d) $(DEBUG_OBJ:.o=.d) build/release/main.d \
    build/debug/main.d build/release/embed.d build/release/least.d
