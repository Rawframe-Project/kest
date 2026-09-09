#ifndef KEST_MEM_H
#define KEST_MEM_H

#include <stdbool.h>
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

// Makes the last thing handed out bigger, when it is the last thing handed out.
// Answers where it is now, which is where it was when the block it is in had
// the room, and somewhere else when the thing had a block to itself and the
// block was made bigger. Answers NULL when neither, and then the caller does
// what it did before: takes a new one and copies.
//
// What it gains is nought, like everything else handed out. What moves is only
// ever the thing itself, because a block that is made bigger is one nothing
// else is in.
void *kest_arena_extend(KestArena *arena, void *last, size_t was, size_t want);

// Hands everything back at once and keeps the arena, which is what a program
// wants between frames: the block it started with stays, and only what was
// handed out of it is cleared again. Taking a block from the host and giving
// one back every time round a loop is a cost a frame budget can see.
void kest_arena_reset(KestArena *arena);

// Copies len bytes and terminates them, so the result is usable wherever a C
// string is expected.
char *kest_arena_strndup(KestArena *arena, const char *text, size_t len);

// How many bytes have been handed out. What a running program allocated is
// the cost D012 defers, and a number is what makes it a thing a host can see
// rather than a thing to argue about.
size_t kest_arena_used(const KestArena *arena);

// What the allocation this arena last refused was asking for, and nought when
// it has refused nothing. A ceiling stops a program at the allocation that
// would have crossed it, so what was handed out stops short of the ceiling by
// this much: the two numbers are the same number said from either side.
size_t kest_arena_refused(const KestArena *arena);

// Whether this arena handed out the address: inside one of its blocks and
// below what that block has given away. A machine asks it about a pointer it
// was handed from outside, because reading one it never gave out is reading
// whatever is at that address — and what a handle is checked for is four bytes
// at the front, which any four bytes can be.
//
// What all the blocks sit between answers most of it without a walk, and the
// block that answered last answers the rest of it: the same handle crosses
// every frame. What is left is a walk, which is why it is asked at a boundary
// crossing and not at an instruction.
bool kest_arena_holds(KestArena *arena, const void *at);

// The most this arena will ever hand out. Zero is none, which is what an arena
// has until somebody says otherwise. Past it an allocation answers NULL, which
// is what every caller already handles, because the alternative is a caller
// that handles running out one way and being capped another.
void kest_arena_cap(KestArena *arena, size_t bytes);

#define KEST_ARENA_NEW(arena, type)                                            \
    ((type *)kest_arena_alloc((arena), sizeof(type), _Alignof(type)))

#define KEST_ARENA_ARRAY(arena, type, count)                                   \
    ((type *)kest_arena_alloc((arena), sizeof(type) * (count), _Alignof(type)))

#endif
