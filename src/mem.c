#include "mem.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE (64 * 1024)

typedef struct Block {
    struct Block *next;
    size_t used;
    size_t capacity;
    unsigned char data[];
} Block;

struct KestArena {
    Block *head;
    // Kept rather than counted, because a ceiling is asked about at every
    // allocation and walking the blocks to answer would make an arena slower
    // the longer a program runs.
    size_t handed;
    size_t ceiling;
};

static Block *block_new(size_t capacity) {
    Block *block = calloc(1, sizeof(Block) + capacity);
    if (block == NULL) {
        return NULL;
    }
    block->capacity = capacity;
    return block;
}

KestArena *kest_arena_new(void) {
    KestArena *arena = calloc(1, sizeof(KestArena));
    if (arena == NULL) {
        return NULL;
    }
    arena->head = block_new(BLOCK_SIZE);
    if (arena->head == NULL) {
        free(arena);
        return NULL;
    }
    return arena;
}

void kest_arena_free(KestArena *arena) {
    if (arena == NULL) {
        return;
    }
    Block *block = arena->head;
    while (block != NULL) {
        Block *next = block->next;
        free(block);
        block = next;
    }
    free(arena);
}

void *kest_arena_alloc(KestArena *arena, size_t size, size_t align) {
    size_t offset = (arena->head->used + align - 1) & ~(align - 1);
    bool fresh = offset + size > arena->head->capacity;
    // What this costs, which is the padding as well as the size: a block that
    // is left with a hole in it has handed that hole out to nobody.
    size_t taking = fresh ? size : offset + size - arena->head->used;
    // Asked before a block is taken from the host, so a refusal costs nothing.
    if (arena->ceiling != 0 && arena->handed + taking > arena->ceiling) {
        return NULL;
    }
    if (fresh) {
        size_t capacity = size > BLOCK_SIZE ? size : BLOCK_SIZE;
        Block *block = block_new(capacity);
        if (block == NULL) {
            return NULL;
        }
        block->next = arena->head;
        arena->head = block;
        offset = 0;
    }
    void *result = arena->head->data + offset;
    arena->head->used = offset + size;
    arena->handed += taking;
    return result;
}

size_t kest_arena_used(const KestArena *arena) {
    return arena->handed;
}

void kest_arena_cap(KestArena *arena, size_t bytes) {
    arena->ceiling = bytes;
}

char *kest_arena_strndup(KestArena *arena, const char *text, size_t len) {
    char *copy = kest_arena_alloc(arena, len + 1, 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, len);
    copy[len] = '\0';
    return copy;
}
