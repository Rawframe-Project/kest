#include "mem.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// A block is one allocation as far as the host is concerned, so reading one
// element past the end of something inside it is memory this arena owns and
// nothing anywhere says a word about it. The sanitised build is told instead:
// a block is poisoned when it is taken, each allocation is opened to its own
// size, and a gap is left after it that stays poisoned. Off the end of a thing
// is the read this project has got wrong before, and this is what makes it
// visible.
//
// The release build includes nothing but ISO C. This is a header of the
// sanitiser, in a build that is already standing on it.
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define KEPT_BACK 16
#define POISON(at, bytes) __asan_poison_memory_region((at), (bytes))
#define OPEN(at, bytes) __asan_unpoison_memory_region((at), (bytes))
#else
#define KEPT_BACK 0
#define POISON(at, bytes) ((void)(at), (void)(bytes))
#define OPEN(at, bytes) ((void)(at), (void)(bytes))
#endif

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
    POISON(block->data, capacity);
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
        // Given back the way it was taken: memory left poisoned is memory the
        // host may hand out again and be told about.
        OPEN(block->data, block->capacity);
        free(block);
        block = next;
    }
    free(arena);
}

void kest_arena_reset(KestArena *arena) {
    if (arena == NULL) {
        return;
    }
    // The block this arena started with is the one it keeps, because it is
    // the one that is always there and always the same size. The rest are
    // what a program grew into and what it is being asked to give back.
    Block *first = arena->head;
    while (first->next != NULL) {
        first = first->next;
    }
    Block *block = arena->head;
    while (block != first) {
        Block *next = block->next;
        OPEN(block->data, block->capacity);
        free(block);
        block = next;
    }
    first->next = NULL;
    // Only what was handed out of it, because the rest was never written to
    // and an allocation is promised memory that is nought. Clearing a whole
    // block to give back a hundred bytes is the reset costing more than the
    // work it is undoing.
    OPEN(first->data, first->used);
    memset(first->data, 0, first->used);
    POISON(first->data, first->capacity);
    first->used = 0;
    arena->head = first;
    arena->handed = 0;
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
    // The gap is not handed to anybody, so it is not counted as handed out:
    // what a program is told it used is the same number in both builds, and so
    // is what a ceiling refuses.
    arena->head->used = offset + size + KEPT_BACK;
    arena->handed += taking;
    OPEN(result, size);
    return result;
}

bool kest_arena_extend(KestArena *arena, void *last, size_t was, size_t want) {
    if (arena == NULL || last == NULL || want <= was) {
        return false;
    }
    Block *block = arena->head;
    unsigned char *end = (unsigned char *)last + was;
    // The last thing handed out is the one the block ends at, gap and all.
    // Anything else has something after it, and moving that is not what this
    // is for.
    if (end + KEPT_BACK != block->data + block->used) {
        return false;
    }
    size_t offset = (size_t)((unsigned char *)last - block->data);
    if (offset + want + KEPT_BACK > block->capacity) {
        return false;
    }
    size_t taking = want - was;
    if (arena->ceiling != 0 && arena->handed + taking > arena->ceiling) {
        return false;
    }
    block->used = offset + want + KEPT_BACK;
    arena->handed += taking;
    // What was the gap is now part of the thing, and the gap moves to the end
    // of it.
    OPEN(end, taking);
    POISON((unsigned char *)last + want, KEPT_BACK);
    return true;
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
