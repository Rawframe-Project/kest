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

typedef struct KestBuild KestBuild;

// How what the boundary says is written. Prose for a person, and the same set
// as JSON for whatever reads it after: an editor, a build, a model repairing
// what it wrote. Nothing is in one form and not the other.
typedef enum {
    KEST_FORM_TEXT,
    KEST_FORM_JSON,
} KestForm;

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
//
// A tagged union has no one piece per slot: which type a payload slot holds
// depends on the tag. A value that holds one anywhere says so, and is moved by
// reading the tag first rather than by walking the pieces.
typedef struct {
    const KestPiece *pieces;
    uint16_t count;
    uint16_t size;
    uint16_t align;
    const void *type;
    bool tagged;
} KestLayout;

// What the machine is allowed. Zero means the built-in number, which is what
// a host that has no opinion gets, and for the heap it means whatever the host
// itself can spare.
//
// The heap is the one of the three that grows while a program runs, so it is
// the one a host watching a frame budget puts a number on: crossing it is a
// message at the instruction that asked, in the same shape as anything else
// that fails while running, rather than a machine that has taken the memory
// the host wanted for something else.
typedef struct {
    uint32_t stack_slots;
    uint32_t call_depth;
    size_t heap_bytes;
} KestLimits;

// Why there is a least, or why there is not. A run of calls that comes back
// round has no deepest frame, and a call through a function value reaches
// something that is not known until it runs; those are two different things to
// be told, because one is a shape a host can change and the other is a number
// a host has to pick.
typedef enum {
    KEST_REACH_KNOWN,
    KEST_REACH_ITSELF,
    KEST_REACH_VALUE,
    // Nothing was asked: the build did not compile, or there was no room to
    // work it out.
    KEST_REACH_UNASKED,
} KestReach;

// What working the least out found, and the function it found it in. The name
// is the program's own: one that takes something carries what it takes, which
// is how one copy of a generic is told from another.
typedef struct {
    KestReach reach;
    const char *where;
} KestReason;

// The least this program can be given, worked out from what it calls. It is
// enough for every function the host could call, not the least for one of
// them, because a host does not want a different answer per call site.
//
// False when there is no answer, and `why` says which of the reasons above it
// was and where. A host that gets false picks a number and finds out, which is
// what every host did before this; `why` may be NULL for a host that only
// wants to know whether to.
//
// It answers for one call in. A host whose bound function calls back in adds
// room for what that starts, because how many times it will is the host's to
// know and not the program's.
bool kest_needs(KestBuild *build, KestLimits *least, KestReason *why);

// The machine, while it is running. A host function is handed one so that it
// can give the program a view of memory the host owns.
typedef struct KestRuntime KestRuntime;

// A function the host provides. Its arguments are the slots at `frame`, laid
// out the way the declaration says, and it writes its result over them. A
// value of more than one slot occupies that many, so a `Vec3` argument is
// three and a returned one replaces the first three.
// `context` is whatever was given when the function was bound, which is how a
// host reaches its own state from inside one.
typedef void (*KestNative)(KestValue *frame, KestRuntime *runtime,
                           void *context);

// Hands the program an array over memory the host owns. Nothing is copied and
// nothing is freed: the caller keeps the block and must outlive the program's
// use of it.
//
// `element` is what the program calls the type, and `size` is what this host
// thinks one is. The stride comes from the program, so it cannot be wrong;
// `size` is here to be disagreed with. A host that has a different idea of the
// shape is told, and gets a value whose `object` is NULL, rather than reading
// the block as something it is not.
KestValue kest_borrow(KestRuntime *runtime, void *data, uint32_t length,
                      const char *element, size_t size);


// Calls a function the program defines, by the name it lives under. `frame`
// holds the arguments laid out the way the declaration says and receives the
// result over them, which is the same convention a host function is called
// with, in the other direction.
//
// `entry` is what `kest_entry` gave for the name. `slots` is how many
// `KestValue`s `frame` holds; the program says how many it needs, so a frame
// that is too narrow is a message rather than a read past the end of the
// host's array.
//
// D007 measured the outward crossing as the wider of the two, so the shape to
// reach for is one call carrying a batch rather than one call per item.
// Returns false when the program failed while running, which is reported into
// the diagnostics the runtime was made with.
// `frame` has to be wide enough for whichever is larger, what is passed or
// what comes back, because they are the same slots.
//
// No frame is a frame of no slots, and is what to pass for a function that
// takes nothing and gives nothing back. Anything else is refused rather than
// run on whatever the stack was left holding.
bool kest_call(KestRuntime *runtime, int32_t entry, KestValue *frame,
               uint32_t slots);

// Where a function lives in this program, or -1 when there is none of that
// name. Finding a name is a search over everything the program defines, so it
// is done once and a frame calls by what it found. This is also how a host
// asks whether the program defines something, which is why a name nothing
// knows is -1 and nothing else: asking is allowed.
//
// Two names are here and still cannot be handed over, and those say why into
// `kest_report`: a generic, which is compiled once for each set of types it is
// used with and so is several functions rather than one, and a function the
// host itself provides, which crosses the other way. Both would otherwise send
// a host looking for a typo.
//
// The name is the one the file writes. A file that says `module game.world`
// registers its `spawn` as `world.spawn`, and this finds it either way.
int32_t kest_entry(KestRuntime *runtime, const char *name);

// How many arguments this takes, and where the one at `which` starts in the
// frame, in slots. A value is one slot a scalar, so a `Vec2` is two and the
// second one of them starts at two; asking beats counting the fields of the
// first, which is what a host would otherwise be doing with a number the
// program already knows.
//
// `kest_frame_at` answers how wide the arguments are together when `which` is
// past the last one, which is where a result written over them would start.
uint32_t kest_frame_takes(KestRuntime *runtime, int32_t entry);
uint32_t kest_frame_at(KestRuntime *runtime, int32_t entry, uint32_t which);

// What the argument at `which` is, or NULL past the last one. It is the same
// layout `kest_build_layout` gives for a type by name, so a host checks an
// argument the way it checks something it lends: the bytes, and where each
// piece of it sits. Writing the right number of slots with the wrong things
// in them is the mistake this is for.
const KestLayout *kest_frame_layout(KestRuntime *runtime, int32_t entry,
                                    uint32_t which);

// How wide a frame has to be to call this: enough for what it takes and for
// what it gives back, whichever is more.
//
// Zero for an index that is no function, and zero for a function that takes
// nothing and gives nothing, because that is what it needs. The number cannot
// tell those apart and so the first of them says so into `kest_report`: ask
// this about what `kest_entry` answered, and check that first.
uint32_t kest_frame_slots(KestRuntime *runtime, int32_t entry);

// Writes what the program has said since the last time this was asked: what
// failed while running, and what a lend disagreed about. A host that gets
// `false` from `kest_call`, or a lend whose `object` is NULL, calls this to
// find out why. Nothing is written twice, and nothing from before this machine
// started is written at all: what failed to compile went to `kest_build`.
//
// A machine that has said nothing since it was last asked writes nothing, in
// either form, because that is what it has to say. `KEST_FORM_JSON` writes one
// object per call for the run of diagnostics that call is about, so a host
// asking after every call gets one line each.
void kest_report(KestRuntime *runtime, FILE *out, KestForm form);

// How many bytes the running program has allocated. Nothing frees them, so
// this only goes up, and a host watching it is watching the cost D012 defers.
size_t kest_heap_used(const KestRuntime *runtime);

// What this machine is actually running with, which is what the host asked for
// where it asked and the built-in number where it did not. Those numbers are
// otherwise not knowable: a host that passed nothing has no way to write down
// what it got, and `kest_heap_used` is a number without a scale until the
// ceiling beside it is readable.
//
// `heap_bytes` answers zero when there is no ceiling, because that is what no
// ceiling is. The other two are always a number, because a machine always has
// a stack and a depth.
void kest_allowed(const KestRuntime *runtime, KestLimits *limits);

// Throws the heap away and starts it again. Nothing in the machine survives a
// call, so between calls there is nothing of the program's left to point at
// it; what this invalidates is every handle the *host* is still holding. An
// array, a store or a piece of text that came out of `kest_call` is gone
// after this, and passing one back in is reading freed memory.
//
// Between calls, and not inside one. A bound function that asks for this from
// inside the call it was called from is asking for what the program is
// standing on, and is refused: `kest_report` says so.
//
// Returns false when the host is out of memory, and the runtime is unusable
// if it does, or when the program is running.
bool kest_heap_reset(KestRuntime *runtime);

// What the host provides, bound by the name the program declares:
// `extern fn Clock.now() -> i64` is bound as "Clock.now".
typedef struct KestHost KestHost;

KestHost *kest_host_new(void);
void kest_host_free(KestHost *host);

// Returns false when the host is out of memory, and when the name is already
// bound: a name is bound once. A machine takes what the host held when it
// started and keeps it, so binding after `kest_start` would change the table
// and not the machine, and answering that it had worked would be a lie half
// the time.
bool kest_host_bind(KestHost *host, const char *name, KestNative function,
                    void *context);

// The function bound to a name, or NULL. A program that declares something
// the host does not provide is refused before it runs, by name.
KestNative kest_host_find(const KestHost *host, const char *name,
                          void **context);

// A compiled program, and everything it was compiled from. One of these is
// what a host has instead of the stages there are.
// Compiles a file and everything it imports. Diagnostics go to `errors` in the
// form asked for, or nowhere when that is NULL. `library` is where `std`
// lives, or NULL for `lib/` beside the program. Returns NULL when it did not
// compile.
KestBuild *kest_build(const char *path, const char *library, FILE *errors,
                      KestForm form);
void kest_build_free(KestBuild *build);

// A name the program asks the host for, by position, or NULL past the last of
// them. A host walks from zero until NULL to learn every one.
//
// `kest_start` refuses a program whose externs are not all bound, and says
// which by name. This is the same list before the refusal, for a host that
// embeds a program it did not write and would otherwise learn the names one
// failed start at a time. It is asked of the build, because that is what a
// host has before there is a machine.
const char *kest_build_extern(const KestBuild *build, uint32_t at);

// What the program lays a type out as where memory is shared, by the name a
// host would lend it under. Answers how many types of that name the program
// has, and fills `layout` when that is one: nought is a name it does not hold
// in an array, which is exactly the set that cannot be lent, and more than one
// is a name that needs the module written in front of it, `world.Event`.
//
// This is what `kest_borrow` will compare a host's own `sizeof` against, asked
// before the lend rather than found out at one. A host lending in a loop
// checks once. The layout is the build's and lasts until `kest_build_free`.
uint32_t kest_build_layout(const KestBuild *build, const char *name,
                           const KestLayout **layout);


// A machine for a compiled program. The build has to outlive it, and
// `limits` may be NULL. Free it with `kest_runtime_free`.
//
// A build makes as many machines as a host wants. Each has its own stack,
// heap and diagnostics, and what they share is the compiled program, which
// nothing writes to once it is compiled. What one says is not what another
// reports.
KestRuntime *kest_start(KestBuild *build, const KestHost *host,
                        const KestLimits *limits);

// After the call it was made for returns. A bound function that frees the
// machine from inside one is refused and told, because the frames and the
// stack are what the program is standing on; the heap then waits for
// `kest_build_free`.
void kest_runtime_free(KestRuntime *runtime);


#endif
