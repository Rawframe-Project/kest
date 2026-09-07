CC ?= cc
WARN := -std=c11 -Wall -Wextra -Werror
SRC := $(wildcard src/*.c)

RELEASE_OBJ := $(SRC:src/%.c=build/release/%.o)
DEBUG_OBJ := $(SRC:src/%.c=build/debug/%.o)

# Each configuration keeps its own objects, so switching between them cannot
# link one build's objects with the other's flags.
kest: $(RELEASE_OBJ)
	$(CC) -o $@ $^

build/release/%.o: src/%.c | build/release
	$(CC) $(WARN) -O2 -Iinclude -MMD -MP -c -o $@ $<

kest-debug: $(DEBUG_OBJ)
	$(CC) -fsanitize=address,undefined -o $@ $^

build/debug/%.o: src/%.c | build/debug
	$(CC) $(WARN) -O0 -g -fsanitize=address,undefined -Iinclude -MMD -MP -c -o $@ $<

build/release build/debug:
	mkdir -p $@

debug: kest-debug

clean:
	rm -rf build kest kest-debug

.PHONY: debug clean

-include $(RELEASE_OBJ:.o=.d) $(DEBUG_OBJ:.o=.d)
