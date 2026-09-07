#ifndef KEST_H
#define KEST_H

#include <stdbool.h>
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

#endif
