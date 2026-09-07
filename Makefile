CC ?= cc
WARN := -std=c11 -Wall -Wextra -Werror
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
	$(CC) $(WARN) -O2 -Iinclude -MMD -MP -c -o $@ $<

kest-debug: build/debug/main.o $(DEBUG_OBJ)
	$(CC) -fsanitize=address,undefined -o $@ $^ -lm

build/debug/%.o: src/%.c | build/debug
	$(CC) $(WARN) -O0 -g -fsanitize=address,undefined -Iinclude -MMD -MP -c -o $@ $<

build/release build/debug:
	mkdir -p $@

# A host that is not this command line.
examples/embed: examples/embed.c libkest.a
	$(CC) $(WARN) -O2 -Iinclude -o $@ $< libkest.a -lm

debug: kest-debug
embed: examples/embed

clean:
	rm -rf build kest kest-debug libkest.a examples/embed

.PHONY: debug embed clean

-include $(RELEASE_OBJ:.o=.d) $(DEBUG_OBJ:.o=.d) build/release/main.d build/debug/main.d
