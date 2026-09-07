#ifndef KEST_H
#define KEST_H

#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#define KEST_VERSION_MAJOR 0
#define KEST_VERSION_MINOR 1
#define KEST_VERSION_PATCH 0
#define KEST_VERSION_STRING "0.1.0"

// Returns the version this library was built as, for a host that links against
// a Kest it did not compile itself.
const char *kest_version(void);

// A runtime value carries no tag. The language is statically typed, so an
// instruction knows what it is operating on and a host function knows what it
// was declared to take.
typedef union {
    int64_t integer;
    double real;
    bool boolean;
    const char *text;
    void *object;
} KestValue;

// What one scalar inside a value is, where memory is shared.
typedef enum {
    KEST_L_I8,
    KEST_L_I16,
    KEST_L_I32,
    KEST_L_I64,
    KEST_L_U8,
    KEST_L_U16,
    KEST_L_U32,
    KEST_L_U64,
    KEST_L_F32,
    KEST_L_F64,
    // A handle, a piece of text or a reference: a machine word as it is.
    KEST_L_WORD,
} KestScalar;

typedef struct {
    uint16_t offset;
    uint8_t kind;
} KestPiece;

// How a value is laid out in memory, as against how it sits on the stack. One
// piece per slot, in slot order, so unpacking an element is a walk of this.
typedef struct {
    const KestPiece *pieces;
    uint16_t count;
    uint16_t size;
    uint16_t align;
} KestLayout;

// What the machine is allowed. Zero means the built-in number, which is what
// a host that has no opinion gets.
typedef struct {
    uint32_t stack_slots;
    uint32_t call_depth;
} KestLimits;

// The machine, while it is running. A host function is handed one so that it
// can give the program a view of memory the host owns.
typedef struct KestRuntime KestRuntime;

// A function the host provides. Its arguments are the slots at `frame`, laid
// out the way the declaration says, and it writes its result over them. A
// value of more than one slot occupies that many, so a `Vec3` argument is
// three and a returned one replaces the first three.
typedef void (*KestNative)(KestValue *frame, KestRuntime *runtime);

// Hands the program an array over memory the host owns. Nothing is copied and
// nothing is freed: the caller keeps the block and must outlive the program's
// use of it. `stride` is the size of one element, which is the size the
// program's element type has, and D016 is why those are the same number.
KestValue kest_borrow(KestRuntime *runtime, void *data, uint32_t length,
                      uint16_t stride);

// Calls a function the program defines, by the name it lives under. `frame`
// holds the arguments laid out the way the declaration says and receives the
// result over them, which is the same convention a host function is called
// with, in the other direction.
//
// D007 measured the outward crossing as the wider of the two, so the shape to
// reach for is one call carrying a batch rather than one call per item.
// Returns false when the program failed while running, which is reported into
// the diagnostics the runtime was made with.
// `frame` has to be wide enough for whichever is larger, what is passed or
// what comes back, because they are the same slots.
bool kest_call(KestRuntime *runtime, const char *name, KestValue *frame);

// Whether the program defines a function under this name.
bool kest_defines(const KestRuntime *runtime, const char *name);

// How many bytes the running program has allocated. Nothing frees them, so
// this only goes up, and a host watching it is watching the cost D012 defers.
size_t kest_heap_used(const KestRuntime *runtime);

// Throws the heap away and starts it again. Nothing in the machine survives a
// call, so between calls there is nothing of the program's left to point at
// it; what this invalidates is every handle the *host* is still holding. An
// array, a store or a piece of text that came out of `kest_call` is gone
// after this, and passing one back in is reading freed memory.
//
// Returns false only when the host is out of memory, and the runtime is
// unusable if it does.
bool kest_heap_reset(KestRuntime *runtime);

// What the host provides, bound by the name the program declares:
// `extern fn Clock.now() -> i64` is bound as "Clock.now".
typedef struct KestHost KestHost;

KestHost *kest_host_new(void);
void kest_host_free(KestHost *host);

// Returns false only when the host is out of memory. Binding a name twice
// keeps the last one.
bool kest_host_bind(KestHost *host, const char *name, KestNative function);

// The function bound to a name, or NULL. A program that declares something
// the host does not provide is refused before it runs, by name.
KestNative kest_host_find(const KestHost *host, const char *name);

// A compiled program, and everything it was compiled from. One of these is
// what a host has instead of the stages there are.
typedef struct KestBuild KestBuild;

// Compiles a file and everything it imports. Diagnostics go to `errors`, or
// nowhere when that is NULL. `library` is where `std` lives, or NULL for
// `lib/` beside the program. Returns NULL when it did not compile.
KestBuild *kest_build(const char *path, const char *library, FILE *errors);
void kest_build_free(KestBuild *build);

// The name something lives under in the file that was compiled: a `main` in
// `module game.world` is `world.main`, which is what `kest_call` wants.
const char *kest_build_name(KestBuild *build, const char *name);

// A machine for a compiled program. The build has to outlive it, and
// `limits` may be NULL. Free it with `kest_runtime_free`.
KestRuntime *kest_start(KestBuild *build, const KestHost *host,
                        const KestLimits *limits);
void kest_runtime_free(KestRuntime *runtime);


#endif
