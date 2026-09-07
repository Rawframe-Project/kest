CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O2
LDFLAGS ?=

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
DEP := $(OBJ:.o=.d)

kest: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -Iinclude -MMD -MP -c -o $@ $<

debug: CFLAGS := -std=c11 -Wall -Wextra -Werror -O0 -g -fsanitize=address,undefined
debug: LDFLAGS := -fsanitize=address,undefined
debug: clean kest

clean:
	rm -f kest $(OBJ) $(DEP)

.PHONY: debug clean

-include $(DEP)
