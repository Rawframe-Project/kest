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

// A function the host provides. Its arguments are the slots at `frame`, laid
// out the way the declaration says, and it writes its result over them. A
// value of more than one slot occupies that many, so a `Vec3` argument is
// three and a returned one replaces the first three.
typedef void (*KestNative)(KestValue *frame);

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
