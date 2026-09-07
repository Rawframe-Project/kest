#include "mem.h"

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
    if (offset + size > arena->head->capacity) {
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
    return result;
}

size_t kest_arena_used(const KestArena *arena) {
    size_t total = 0;
    for (const Block *block = arena->head; block != NULL; block = block->next) {
        total += block->used;
    }
    return total;
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
