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

// Every function declared here is called by one of the two hosts written
// against it, so there is somewhere to look for each: `src/main.c` is a
// command line — it compiles, runs, calls one function, ticks a program and
// reports — and `examples/embed.c` is an engine, which keeps a world between
// frames, lends its own memory, binds what a program asks of it and watches
// what a frame costs. `tools/check-dead.sh` holds that to being true rather
// than leaving it a claim.

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
    // What a case of an enum carries, which the tag beside it says. A host
    // reading a layout switches on the tag and knows what is there; what it
    // must not do is read it as the word a handle is, which is what this said
    // before it had a name of its own.
    KEST_L_PAYLOAD,
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

// The name of the one function this language knows about. The checker holds a
// function of this name in the file that was named to the shape a host can
// call — nothing in, a number or nothing back — so a host that wants to call
// the entry point writes this rather than a string of its own.
#define KEST_MAIN "main"

// What a machine is given when a host says nothing, so that a host can say
// what it means by "as much as usual" rather than only by leaving a zero.
#define KEST_STACK_SLOTS 65536
#define KEST_CALL_DEPTH 1024

// What the machine is allowed. Zero means the two numbers above, which is what
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

// The least for one function and what it reaches, for a host that knows which
// ones it calls. `name` is what the file wrote, with or without the module in
// front of it, which is the name `kest_entry` takes.
//
// False when there is no such function, as well as for the two reasons above.
// A host that calls several asks about each and takes the largest, because
// which of them it will call and in what order is the host's to know.
bool kest_needs_of(KestBuild *build, const char *name, KestLimits *least,
                   KestReason *why);

// Where the machine already is when it calls into the host: the frames and
// slots in use at the deepest place `name` reaches a host function. A host
// function that calls back in with `kest_call` starts from there and not from
// nothing, so what a re-entrant host needs is this plus what the entry it
// calls needs on its own — `kest_needs_of` for that one, added to this.
//
// Both are nought when nothing `name` reaches calls into the host, and then
// there is nowhere to call back in from. The heap is not part of it: it does
// not nest.
//
// `name` may be NULL, and then the answer is over every function the program
// defines, which is what a host that calls more than one has to be given.
//
// False for the same reasons `kest_needs_of` is false, and for the same name.
bool kest_needs_from(KestBuild *build, const char *name, KestLimits *inside,
                     KestReason *why);

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

// Hands the program a piece of text. The bytes are copied into the machine's
// heap, which is where the program's own text lives, so nothing is promised
// about the host's copy afterwards: a host that handed a pointer of its own
// would be undertaking to keep it as long as the program holds it, and a
// program holds a piece of text for as long as it likes.
//
// Text ends at its first zero byte, so a zero inside `length` is a mistake
// rather than a cut: it is reported and what comes back is empty. So is what
// comes back when the heap is full, which is the other way this can fail.
KestValue kest_text(KestRuntime *runtime, const char *bytes, uint32_t length);

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

// And the end of a lend, which is the host saying the block is not its to lend
// any more. Nothing is freed: the block was the host's throughout. What
// changes is what the program holds — every use of it afterwards is a message
// at the instruction that used it rather than a read of memory the host has
// moved on from.
//
// A host lending what it owns for a frame calls this at the end of the frame.
// Without it the header says nothing about how long the block was good for,
// and the program's copy of the handle outlives whatever the host did next.
//
// True when the lend was ended. False when the value is not a lend this
// machine gave out, and when it is an array the program made rather than one
// the host lent, which is not the host's to end; both say why into
// `kest_report`.
bool kest_lend_ends(KestRuntime *runtime, KestValue lent);

// A handle to a lend that has ended is dead the moment it ends, and stays dead
// only until the next lend. What a lend costs is a header, and the header a
// lend gives back is the header the next one gets — so a handle kept past the
// end of its lend names whatever was lent after it, and the machine cannot
// tell: a lend handle is a pointer, and unlike a reference into a store it
// carries no stamp to say which lend it is a handle to. Ending a lend is where
// a host drops the handle, not a thing it does before using one more time.
// See D352.

// Whether what a host kept is still the machine's to read. A host function is
// handed the program's values and may keep one past the call: a piece of text,
// an array, a store. They last as long as the heap they are on, which is as
// long as nothing throws it away — and a host that threw it away is the only
// one who knows, which is one thing too many to have to remember.
//
// True while the machine still has the memory it handed out. False after
// `kest_heap_reset`, and false for anything this machine never gave the host.
//
// It says nothing about what is written there: a lend the host itself ended is
// still the machine's memory, and the host that ended it knows it did. What
// this answers is the one thing a host cannot see for itself.
bool kest_still_holds(const KestRuntime *runtime, KestValue kept);


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
// `kest_report`: a name that is several functions, because two may share one
// when they take different things and because a generic is compiled once for
// each set of types it is used with; and a function the host itself provides,
// which crosses the other way. Both would otherwise send a host looking for a
// typo.
//
// The first names them, and those names are the program's own: `add#i32,i32`
// is what to ask for, and `kest_frame_layout` says what the one that came back
// takes.
//
// The name is the one the file writes. A file that says `module game.world`
// registers its `spawn` as `world.spawn`, and this finds it either way.
int32_t kest_entry(KestRuntime *runtime, const char *name);

// The one at `at` of the functions of that name, or -1 past the last. A name
// that is one function is that function at nought and nothing after it.
//
// This is what to walk when `kest_entry` says a name is several functions: a
// host asks each of them what it takes, with `kest_frame_layout`, and calls
// the one it meant. It does not have to know how the compiler spells a name
// that carries what it takes.
int32_t kest_entry_of(KestRuntime *runtime, const char *name, uint32_t at);

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

// And what comes back over them, or NULL when the function gives nothing. The
// same layout again, so a host reads a result knowing what it is rather than
// knowing how wide it is.
const KestLayout *kest_frame_gives(KestRuntime *runtime, int32_t entry);

// What this host is about to write, said back to the program before it writes
// it: one kind a slot, in the order the arguments are laid out, which is the
// order `kest_frame_layout` gives them in. A frame of the right width with the
// wrong things in it is the mistake this is for — `kest_call` can see how wide
// a frame is and not what a host meant to put in it.
//
// `count` has to be every slot the arguments take: saying what some of them
// hold is not checking the rest, and a host that stops short is told so rather
// than told nothing.
//
// The kinds are the ones a layout is made of, and a slot holds what a piece of
// that type holds: a struct of three `f32` is three slots of `KEST_L_F32`,
// each read as a `double` in the slot, which is what the layout said and this
// says again where a host can be wrong about it.
//
// True when they agree. False when they do not, and when there is nothing at
// `entry`; both say why into `kest_report`.
bool kest_frame_fills(KestRuntime *runtime, int32_t entry,
                      const uint8_t *kinds, uint32_t count);

// And what this host is about to read back out of one, which is the same
// mistake in the other direction: a slot holding a float read as a number of
// the host's own is a number nobody wrote. One kind a slot again, over what
// the function gives back, which is nothing at all for one that gives nothing.
//
// True when they agree, and false in the same three ways.
bool kest_frame_reads(KestRuntime *runtime, int32_t entry,
                      const uint8_t *kinds, uint32_t count);

// The arguments handed over as words, written the way a program writes them:
// `12`, `1.5`, `true`, and a piece of text as itself. The machine reads each
// one as the type the declaration says and lays them out in `frame`, so a host
// that hands over words has no slots to be wrong about — the other half of
// `kest_gave_text`, which says what a frame holds without a host reading one.
//
// `words` is one a parameter and not one a slot, and `count` has to be how
// many the function takes. `slots` is how wide `frame` is, which has to be at
// least what the function takes and what it gives back, the same as
// `kest_call`.
//
// What cannot be written as a word is refused rather than guessed at: a struct,
// an array, a store, a reference, a handle. A host with one of those lays out
// the slots itself and says what it wrote with `kest_frame_fills`.
//
// True when the frame holds them. False when a word is not what the function
// takes, when there is the wrong number of them, and when there is nothing at
// `entry`; each says why into `kest_report`.
bool kest_takes_text(KestRuntime *runtime, int32_t entry, KestValue *frame,
                     uint32_t slots, const char *const *words, uint32_t count);

// What came back, written the way the language writes a value in a hole: `12`,
// `true`, `Door.Shut`, `State.Moving | State.Armed`. Text on its own is what it
// holds and not the source that spells it.
//
// The number of bytes it needs, not counting the end, whatever `room` was —
// the same answer `snprintf` gives, so a host that got a number too big for
// its buffer asks again with one that fits. `out` holds as much as it can with
// an end on it.
//
// This says what is in the frame rather than putting something there: a host
// calls with `kest_call` and then asks. A frame nothing has been called with
// is `K0632` and minus one, rather than a read of whatever the frame was made
// out of.
//
// Nought less than nothing — minus one — for a function that gives nothing,
// and for one that gives something the language has no text of its own for: a
// struct, a run, a store, a reference. A host that wants those written walks
// them with `kest_frame_gives` and writes what it finds, because what a
// program means by them is the host's to decide.
int64_t kest_gave_text(KestRuntime *runtime, int32_t entry,
                       const KestValue *frame, char *out, size_t room);

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
//
// Asked on either side of a call, the difference is what that call cost, which
// is the number a host with a frame budget wants: a total is a number without
// a scale, and a frame is what a host has to fit into. A call into a function
// that promises `no.alloc` answers nought, which is that promise read from
// outside rather than taken on faith.
size_t kest_heap_used(const KestRuntime *runtime);

// And what the allocation that was refused was asking for, or nought when
// nothing has been refused. A ceiling stops a program at the allocation that
// would have crossed it, so what it has used stops short of what it was
// allowed — by this much, which is the difference between a frame that missed
// by eight bytes and one that missed by a megabyte. A host raising a ceiling
// reads it to know what it is raising it by.
size_t kest_heap_wanted(const KestRuntime *runtime);

// Which of the two said no. `kest_heap_wanted` is one number for two things
// that happened, and a host does something different about each: a ceiling it
// set is a number it can raise, and a machine with nothing left is not. The
// machine knows which; this is it saying so.
typedef enum {
    // Until something has been refused, which is most of the time.
    KEST_REFUSED_NOTHING,
    // The ceiling this host gave in `KestLimits`, which is a promise this
    // machine kept.
    KEST_REFUSED_CEILING,
    // The machine underneath, which had nothing left to give. Raising the
    // ceiling changes nothing about this one.
    KEST_REFUSED_MACHINE,
} KestRefusal;

KestRefusal kest_heap_refused_by(const KestRuntime *runtime);

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
// That is the only false. It was once a new heap and a free of the old one,
// which the host could be out of memory for; it is the same heap emptied now,
// so it asks the host for nothing and there is nothing else it can fail at.
// See D322.
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
// Frees the build and everything on it. Answers whether there is no build now:
// true when it freed one and true when there was none, false when a machine is
// still standing on it. The program the machines run is on here, and so is
// every piece of text their diagnostics point at, so this is refused while any
// of them is up rather than left to be found out about afterwards: free the
// machines with `kest_runtime_free`, then the build. `kest_build_report` says
// which it was. See D324.
bool kest_build_free(KestBuild *build);

// What the build has said and nobody has been told yet, in the form asked for.
// A build that compiled says nothing here, and then says something when a
// machine fails to start on it: what the program asks the host for and the
// host has not got is settled before anything runs, and there is no machine to
// ask about it afterwards. A host that gets NULL from `kest_start` calls this.
//
// Nothing is written twice: what was written when it was said is not written
// again.
void kest_build_report(KestBuild *build, FILE *out, KestForm form);

// A name the program asks the host for, by position, or NULL past the last of
// them. A host walks from zero until NULL to learn every one.
//
// `kest_start` refuses a program whose externs are not all bound, and says
// which by name. This is the same list before the refusal, for a host that
// embeds a program it did not write and would otherwise learn the names one
// failed start at a time. It is asked of the build, because that is what a
// host has before there is a machine.
const char *kest_build_extern(const KestBuild *build, uint32_t at);

// What the program expects the one at `at` to take and to give back: how many
// arguments, what each of them is, and what comes back over them. The same
// layouts `kest_frame_layout` gives for a function the host calls, because a
// crossing is the same shape whichever way it goes.
//
// A host binds a C function to a name and nothing else checks that the two
// agree about what crosses: a function bound to a name that takes two things
// and written to take three reads whatever is beside them. This is the check,
// and it is asked of the build, because a host binds before there is a
// machine. `kest_extern_gives` is NULL for one that gives nothing.
uint32_t kest_extern_takes(const KestBuild *build, uint32_t at);
const KestLayout *kest_extern_layout(const KestBuild *build, uint32_t at,
                                     uint32_t which);
const KestLayout *kest_extern_gives(const KestBuild *build, uint32_t at);

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


// What has to outlive what, which is the whole of it: the build outlives the
// machine, and nothing else has to outlive anything. Starting reads what the
// host bound and keeps its own copy, so a host may be freed as soon as a
// machine has started — `examples/embed.c` does that, rather than saying so.
// The layouts a build lent are the build's and go with it.
//
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
//
// Answers whether there is no machine now: true when it freed one and true
// when there was none, false when it was refused. A host that reads the
// answer knows what it is still holding without reading the report, and the
// refusal lasts exactly as long as the call it was asked in — return from the
// bound function and free it there. Nothing takes it away by force, so a host
// that asks in a loop and never returns keeps the machine and everything on
// it. See D323.
bool kest_runtime_free(KestRuntime *runtime);


#endif
