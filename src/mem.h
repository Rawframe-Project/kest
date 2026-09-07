#ifndef KEST_MEM_H
#define KEST_MEM_H

#include <stddef.h>

// A bump allocator for everything the compiler produces before the program
// runs: tokens, syntax nodes, types, diagnostic strings. None of it outlives
// compilation, so none of it is freed individually.
typedef struct KestArena KestArena;

KestArena *kest_arena_new(void);
void kest_arena_free(KestArena *arena);

// Returns zeroed memory, or NULL when the host is out of it. Alignment must be
// a power of two.
void *kest_arena_alloc(KestArena *arena, size_t size, size_t align);

// Copies len bytes and terminates them, so the result is usable wherever a C
// string is expected.
char *kest_arena_strndup(KestArena *arena, const char *text, size_t len);

#define KEST_ARENA_NEW(arena, type)                                            \
    ((type *)kest_arena_alloc((arena), sizeof(type), _Alignof(type)))

#define KEST_ARENA_ARRAY(arena, type, count)                                   \
    ((type *)kest_arena_alloc((arena), sizeof(type) * (count), _Alignof(type)))

#endif
