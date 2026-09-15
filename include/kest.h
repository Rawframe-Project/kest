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
// Every member of this is the whole of a slot, and a `bool` crosses as
// `integer`, nought or one. A member narrower than a slot would be one a host
// could read and could not write: writing a byte of a union leaves the other
// seven holding whatever was in them, and what the machine reads is the whole
// slot, so `false` written that way arrives as true. See D557.
typedef union {
    int64_t integer;
    double real;
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
    // And the tag itself: four bytes, read and written as a whole number, and
    // the one piece of a value with a tag in it whose meaning does not depend
    // on another. It said `KEST_L_I32` until D708, which is what it is and not
    // what it means — so a layout could not say where the tags in it were, and
    // a host walking one could not tell the tag of a field from a number
    // beside it. Every other piece says what it is; this one says it too.
    KEST_L_TAG,
    // And the byte an optional keeps after its value, saying whether the value
    // is there. One byte, read and written as a whole number, and the same
    // reason as the tag beside it: it said `KEST_L_U8` until D714, which is
    // what it is and not what it means, so `struct { at: i32?, n: i32 }` and a
    // number, a `bool` and a number were one run of pieces — same kinds, same
    // offsets, same size — and a host could lend either under the other's name.
    KEST_L_HELD,
    // A place in a store, which is not a machine word at all: a reference is
    // the slot it names and the number of times that slot has been handed out,
    // packed into one whole number. It said `KEST_L_WORD` until D715, and the
    // answer that came with that — read it through `text` or `object` — was a
    // pointer made out of a number nobody meant as one. A host reads and writes
    // this through `integer`, and what it is for is telling it apart from the
    // handles it used to be one kind with.
    KEST_L_REF,
} KestScalar;

// And which member of a `KestValue` a slot of one of those kinds is written
// and read through, which is the other reading of the same enum. The kinds are
// the type's own widths — what a piece of it is where memory is shared — and a
// slot is eight bytes whatever that width is, so a host that writes the width a
// kind names writes one byte into eight and the machine reads the other seven.
// See D557: that is the mistake this says out loud instead of leaving to a
// reader.
typedef enum {
    // `KEST_L_F32` and `KEST_L_F64`: `real`, a `double` in the slot either
    // way, which is what a layout of an `f32` array is not.
    KEST_S_REAL,
    // `KEST_L_WORD`: `text` or `object`, whichever the type is. A layout says
    // a machine word and which of the two it is comes from the declaration.
    // `KEST_L_REF` used to be one of these and is not a word: it is a number,
    // and it answers `KEST_S_INTEGER` like every other number.
    KEST_S_WORD,
    // `KEST_L_PAYLOAD`: what the case carries, which the tag beside it says. A
    // host reads the tag first and asks this about the type that came with it.
    KEST_S_TAGGED,
    // Every other kind, however narrow: `integer`, and a `bool` is nought or
    // one.
    KEST_S_INTEGER,
} KestSlot;

// Total for every kind a layout is made of, so a host walking one has an
// answer for each piece rather than for the ones it thought of.
KestSlot kest_slot_of(uint8_t kind);

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

// Which case the tag at a piece of this value names, and what that case
// carries: the name as the program wrote it, and one piece a slot over the
// slots after the tag, at the byte each of them sits at inside the value the
// tag belongs to — which begins where the tag does, so a host that wants those
// bytes inside the whole value adds the tag piece's own offset. It is the
// question `KEST_L_PAYLOAD` leaves open: a payload slot's kind is the tag's to
// say, so the layout cannot say it and this can, once a host has a tag to ask
// about.
//
// `piece` is the piece the tag is, which `KEST_L_TAG` says. A value that is an
// enum has its tag at piece nought; one inside a shape has it wherever the
// fields in front of it end, and either is asked about the same way.
//
// A host filling a frame with an enum writes the tag into that slot and then
// has to know which member of a `KestValue` each slot after it is;
// `kest_slot_of` over these kinds says so, the same as for anything else. A
// host that keeps its own numbers for the cases holds them against this by
// walking the tags up from nought and reading the names.
//
// NULL when the piece is not a tag and when the number is no case of the enum
// whose tag it is. A case that carries nothing answers its name with nought
// pieces. `carries` and `count` may both be NULL for a host that only wants
// the name.
const char *kest_case_of(const KestLayout *layout, uint16_t piece, int32_t tag,
                         const KestPiece **carries, uint16_t *count);

// The name of the one function this language knows about. The checker holds a
// function of this name in the file that was named to the shape a host can
// call — nothing in, a number or nothing back — so a host that wants to call
// the entry point writes this rather than a string of its own.
#define KEST_MAIN "main"

// As much as usual, which a host can say by name rather than by picking a
// number of its own. It is what a machine takes when the program has no answer
// — one that reaches itself, or that calls through a value — and it is what it
// took for everything before D575.
#define KEST_STACK_SLOTS 65536
#define KEST_CALL_DEPTH 1024

// What the machine is allowed. Zero for either of the first two is what the
// program asked for: the worst any function needs, plus the worst call back
// into the program from inside a host function, because a machine does not
// know which function a host will call. A program with no deepest call has no
// number to give, and then zero is the two numbers above. For the heap, zero
// is whatever the host itself can spare.
//
// A host that has no opinion is the one this is for. `kest_needs` is half of
// that question asked before there is a machine: it answers the worst any
// function needs and not the way back in, so a host that asks it and writes
// the answer here gets a smaller machine than one that writes nothing. Adding
// `kest_needs_from` with no name is the whole of it, and the two added is
// exactly what zero here means. A host that wants more than the program asked
// for says so here — a machine that runs out says what it would have needed,
// so a host that finds out here is told what to write.
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
    // A call through a value, where the program turns none of its own
    // functions into one. Which function such a call enters is not known
    // here, but which ones it could enter is: a function becomes a value in
    // one place, and the answer counts the worst of the ones that ever do. A
    // program with none of them can only have been handed one by a host, and
    // then there is nothing to count. A host that hands in something wider
    // than the program's own is told so at the call, in the same words as any
    // other machine asked for more than it was given. See D814.
    KEST_REACH_VALUE,
    // The program defines no function of that name, which is the same news
    // `kest_entry` gives with -1: a host asking about one it cannot call.
    KEST_REACH_NO_NAME,
    // There was no room to work it out. The answer is not that there is no
    // answer — a host that frees something and asks again may be told one.
    KEST_REACH_NO_ROOM,
    // Nothing was asked: a host that handed over nothing to answer about, or
    // a reason nobody has written into yet. A build that did not compile is
    // not one of these and never was — `kest_build` answers NULL for one, so
    // a host holding a build is holding one that compiled. See D566.
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
//
// The heap it answers is nought, and so do the two below: what a program
// allocates is what it is given to work on, and a loop over four events and a
// loop over four thousand are the same program. Nought is written rather than
// left alone, because a field an answer does not touch is one a caller cannot
// tell from one it did — a host's own cap goes on after asking, and what a host
// that has not measured one has is a machine that says what it reached for when
// it runs out. See D724.
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

// The most this program can want, for a host with no names and a ceiling on
// frames. It is `kest_needs` where there is a least and a bound where there is
// not — the same shape as the two below, and `why` says which it gave.
//
// It is what a machine given nothing is sized by, said before there is one: a
// host that wants to know what saying nothing will cost asks this, and gets the
// number the machine would have picked.
//
// False for the reasons `kest_needs` is false.
bool kest_bound(KestBuild *build, uint32_t frames, KestLimits *most,
                KestReason *why);

// The most `name` and what it reaches can want, for a host that has a name and
// a ceiling on frames rather than a program with a least. `frames` is how many
// a host will allow; nought means as many as usual.
//
// A name whose stack can be worked out answers what `kest_needs_of` answers
// and the frames are not looked at: there is a least, and a bound above it is
// a number nobody needs. A name that reaches a run of calls that comes back
// round has none, and what this gives instead is the smaller of two readings
// of a chain of frames — the widest body it reaches, once a frame, and the
// bodies that go round once a frame with the bodies that do not paid for once.
// Both are true of any chain, so the smaller is.
//
// It is a bound and not a least: a machine made from it is big enough and may
// be bigger than anything the program reaches. A host that wants to know which
// it was asks `kest_needs_of` first — this answers true either way, and `why`
// says which, `KEST_REACH_KNOWN` for a least and `KEST_REACH_ITSELF` or
// `KEST_REACH_VALUE` for a bound.
//
// False for the same reasons `kest_needs_of` is false, and for the same name.
bool kest_bound_of(KestBuild *build, const char *name, uint32_t frames,
                   KestLimits *most, KestReason *why);

// Where the machine already is when it calls into the host: the frames and
// slots a machine is holding where `name` reaches a host function: the widest
// the function itself ever gets, plus the worst of the same over everything it
// reaches on the way to one. It is the function's widest rather than the width
// at the call, so a long expression anywhere in the body is in it whether the
// host is called before that expression or after — which makes the number
// large enough always, and makes shortening the line the call is on the wrong
// thing to shorten. A host function that calls back in with `kest_call` starts
// from there and not from nothing, so what a re-entrant host needs is this plus
// what the entry it calls needs on its own — `kest_needs_of` for that one,
// added to this.
//
// Both are nought when nothing `name` reaches calls into the host, and then
// there is nowhere to call back in from. The heap is not part of it: it does
// not nest.
//
// `name` may be NULL, and then the answer is over every function the program
// defines, which is what a host that calls more than one has to be given.
//
// On true `why->where` names the function the deepest call into the host is
// in, and is NULL when nothing reaches one. That is the function a host would
// have to shorten to make the number smaller, and asking about it by name
// answers what it reaches the host at on its own, which is less than this.
// Shortening it means making the function narrower wherever it is widest,
// which is not always where the call is.
//
// False for the same reasons `kest_needs_of` is false, and for the same name.
bool kest_needs_from(KestBuild *build, const char *name, KestLimits *inside,
                     KestReason *why);

// And where a host may be called back in from, for a name with no least. It is
// `kest_needs_from` where there is one and a bound where there is not, the same
// way `kest_bound_of` is `kest_needs_of` — `why` says which, and a host that
// asks this of a program that can answer the other gets the other.
//
// The bound counts only the bodies that reach a host function: a body that
// never reaches one cannot stand in a chain of frames that ends at a host call,
// above it or below it. `name` may be NULL for every function the program
// defines, the same as `kest_needs_from`.
//
// What a re-entrant host wants is this plus `kest_bound_of` for the entry it
// calls, which is the same sum as for a program with a least and made of the
// same two doors.
bool kest_bound_from(KestBuild *build, const char *name, uint32_t frames,
                     KestLimits *inside, KestReason *why);

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
// rather than a cut: it is `K0611` and what comes back is empty. So is what
// comes back for no address to copy from, and for a heap with no room to copy
// into. An empty piece of text is also what a host asking for one gets, so
// which of the three it was is in the report and nowhere else.
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
//
// What it may hold is numbers. A shape holding text, an array, a store, a
// reference or a function value is refused with `K0647`: those are pointers
// into the machine's own memory, and one sitting in the host's block is one
// the machine did not put there, cannot vouch for, and cannot take back when
// the lend ends. Hand those over a frame instead, where `kest_text` makes the
// text the machine's.
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
//
// And it answers about the memory rather than about what was in it. A piece of
// text kept across `kest_heap_reset` is gone, and stays gone only until the
// machine makes something: what it makes goes where that was, so the pointer
// is live again and reads whatever is written there now. It is the same shape
// as a handle to a lend that has ended, for the same reason — a pointer
// carries no stamp — so a host drops what it kept when it throws the heap
// away, rather than asking afterwards. See D353.
bool kest_still_holds(const KestRuntime *runtime, KestValue kept);

// And which of the two places it is in, which is what the answer above is a
// yes to both of. A host keeping a value between frames is choosing between
// two lifetimes with one pointer in its hand: what the program made while
// running goes when the heap does, and what the program was written with is
// in the build and outlasts every reset. Nothing about the pointer says which,
// and asking afterwards is asking about memory that may already be somebody
// else's — so it is asked before it is kept.
typedef enum {
    // Not this machine's at all: a pointer of the host's own, or one from a
    // heap that has been thrown away.
    KEST_KEPT_NOWHERE,
    // On the heap the program runs on, which `kest_heap_reset` empties.
    KEST_KEPT_HEAP,
    // A lend: a header of the machine's, on that same heap, in front of a
    // block that is the host's own. The two halves do not last the same
    // length of time, and this is the answer that says so — the block is
    // there for as long as the host has it, and the handle in front of it
    // goes with the heap like anything else on one. A lend the host has
    // ended is no longer one of these: the header is the machine's memory
    // and nothing is in front of any more.
    KEST_KEPT_LENT,
    // In the build the machine was started from: text the file was written
    // with, there for as long as the build is.
    KEST_KEPT_PROGRAM,
} KestKept;

// Total, and the one answer `kest_still_holds` is read out of, so the two
// cannot come to disagree about what the machine has.
KestKept kest_kept_where(const KestRuntime *runtime, KestValue kept);


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
//
// A name nothing knows is -1 and nothing else. That is the one answer at this
// boundary that means no and says why nowhere, and it is the one where a host
// asked a question rather than made a mistake: whether a program defines
// something is what this is for. `kest_host_bind` is the other, and for a
// different reason — a host has no report to write into.
int32_t kest_entry(KestRuntime *runtime, const char *name);

// The one at `at` of the functions of that name, or -1 past the last. A name
// that is one function is that function at nought and nothing after it.
//
// This is what to walk when `kest_entry` says a name is several functions: a
// host asks each of them what it takes, with `kest_frame_layout`, and calls
// the one it meant. It does not have to know how the compiler spells a name
// that carries what it takes — and for a copy of a generic it could not guess
// it, because the spelling holds the types the copy was compiled for as well
// as the ones it was written with. The refusal `kest_entry` gives spells them
// out, for a host that would rather keep the name than walk again.
//
// -1 is past the last and nothing else: however many a name is, the walk
// reaches all of them.
int32_t kest_entry_of(KestRuntime *runtime, const char *name, uint32_t at);

// The name the function at `entry` is compiled under, or NULL for an index
// that is no function — which is what ends a walk from zero of everything a
// program defines. This is the other direction of `kest_entry`: that one turns
// a name a host wrote into an index, and this says what the program calls the
// thing at one.
//
// A host that knows the names it wants asks for them and keeps what it was
// given. One embedding a program it did not write — a mod, a level, a rule set
// — has no list to ask from, and learning the names one refused lookup at a
// time is a walk of every name the program has for each name it guesses. This
// is the list itself, and the spelling is the one `kest_entry` takes back,
// including what a copy of a generic is compiled under. See D609.
//
// It says nothing at the end: a walk ending is not news, the same as the walk
// of what a program asks the host for. Asking what a frame holds is not a walk
// and does say so.
const char *kest_entry_name(KestRuntime *runtime, int32_t entry);

// And the same function as somebody wrote it: the name without what tells one
// copy of a generic from another, so the two copies of `pick` above are both
// `pick` here and the walk says which functions of the list are one function.
// NULL for an index that is no function, the same as the name above.
//
// This is the spelling every message uses, so a host that reads a refusal and
// a host that reads the list are looking at the same word. It is not always a
// spelling `kest_entry` can take back: a name that is several functions is
// refused, which is the refusal that names the copies. What goes back in is
// the name above. See D610.
const char *kest_entry_wrote(KestRuntime *runtime, int32_t entry);

// The four questions below all answer an index that is no function the way
// they answer a real one that takes nothing, gives nothing, or has nothing
// past its last argument: with nought or with NULL. `K0634` is what says
// which, so a host that got -1 from `kest_entry` and asked anyway is told,
// rather than told about a function that takes and gives nothing.
//
// How many arguments this takes, and where the one at `which` starts in the
// frame, in slots. A value is one slot a scalar, so a `Vec2` is two and the
// second one of them starts at two; asking beats counting the fields of the
// first, which is what a host would otherwise be doing with a number the
// program already knows.
//
// `kest_frame_at` answers how wide the arguments are together when `which` is
// past the last one, which is where a result written over them would start.
// Whether the one at `entry` promised `no.alloc`, which the compiler proved
// against the code it emitted. False past the last function, and false for one
// that made no promise.
//
// It is the one thing about a function a host can act on before calling it: a
// frame step that may reach the heap is one an engine puts somewhere other than
// a frame, or refuses to install at all. What a program costs in other ways —
// how many of its values were worked out where they stand, how much reading it
// cost — is in what `--json` prints, because a host cannot do anything about
// those and a tool reading them can. See D680.
bool kest_entry_promises(KestRuntime *runtime, int32_t entry);

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
// program means by them is the host's to decide. Which of the two it was is
// `K0646` into `kest_report`, naming the type where there is one: the number
// says no and cannot say why, and an index that is no function is a third
// thing that also answers minus one.
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

// The most places one diagnostic shows: the declaration it is about, the other
// declaration of that name, the calls between a promise and the body that broke
// it. A diagnostic with more says how many it left out rather than stopping
// where a reader would take it for the end — `leftOut` in JSON — and a run of
// calls shown under a refusal is this many frames, because a frame is a place.
#define KEST_MOST_PLACES 8

// The most a machine keeps of what nobody has asked for. A run of diagnostics
// is made at this size, so a machine nobody asks never grows the list it was
// given; what it did not keep is counted, and the report says how many there
// were. A host that reads what it is told never meets this. See D618.
#define KEST_MOST_UNREAD 16

// How many bytes the running program has allocated. Nothing frees them, so this
// only goes up, and a host watching it is watching the cost D012 defers. The
// one thing that moves it the other way is `kest_heap_reset`, which throws the
// whole of it away and puts this back to nothing — what a program is holding is
// a different number from what it has asked for, and this is the second.
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
// Compiles a file and everything it imports. `library` is where `std` lives, or
// NULL for `lib/` beside the program. Returns NULL when it did not compile.
//
// What it could not compile goes to `errors` in the form asked for, or nowhere
// when that is NULL. A program that compiled and had something said about it —
// a shape nothing names, a declaration nothing calls — has that waiting in
// `kest_build_report`, because a build that answered is one a host may want to
// say nothing about. Asking costs nothing and says nothing twice. See D631.
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

// What this build cost: how many bytes reading, checking and compiling the
// program took. Nought for no build.
//
// It is the compiler's own work rather than the program's, which is what
// `kest_heap_used` is about — the two never move together. A host that
// compiles at startup pays this once and never thinks about it again; one that
// reloads a file whenever it changes pays it every time, and this is the
// number that says what that costs. A rebuild costs what the first build cost:
// nothing is carried over from one to the next, so a host reading this after a
// reload is reading the same number it read the first time. See D573.
size_t kest_build_cost(const KestBuild *build);

// And what it is still holding, which is what a host that keeps a build around
// is paying for now rather than what it paid to make one. The two differ by
// what a stage left behind for nobody: the tokens a file is read into are dead
// the moment its tree is made, and the trees are dead once every copy has been
// compiled, so both are given back where the stage that reads them ends. A
// build that was checked and not compiled still holds its trees, because the
// compiler is one of the two stages that read them. Nought for no build.
// See D747 and D748.
size_t kest_build_held(const KestBuild *build);

// Which files that cost was paid for, by position, or NULL past the last of
// them. A host walks from zero until NULL to learn every one.
//
// The list is the file the host named and everything that file imports, which
// is not a list a host can work out for itself: an import names a path relative
// to the file that wrote it, and what a program is made of is settled by the
// loader rather than by whoever started it. A host that reloads a program when
// something changes watches these; a host that watched only what it named would
// keep running a program whose library moved under it. See D657.
const char *kest_build_read(const KestBuild *build, uint32_t at);

// How many bytes the one at `at` is, and nought past the last of them. That is
// the file as it was read rather than as it is now, which is the number the
// cost above was paid over.
size_t kest_build_read_bytes(const KestBuild *build, uint32_t at);

// A number that moves when the bytes of the one at `at` move, and nought past
// the last of them. Two files that are the same bytes have the same number and
// two that differ anywhere do not, which is what a size cannot say: two edits
// that keep the length look the same by size.
//
// It is FNV-1a over the file, which is what the language hashes text with. A
// host may keep it, write it down, and compare it with one from another
// machine: it is the bytes and nothing about this run. See D658.
uint64_t kest_build_read_mark(const KestBuild *build, uint32_t at);

// And one number for the program: every file's mark folded in the order they
// were read. Nought for no build and for a build that read nothing.
//
// What it answers is whether this is the same program, which is a question a
// host asks after a reload and when it looks for what it compiled last time.
// Folded here rather than left to a host, because two hosts folding their own
// way would have two numbers for one program.
uint64_t kest_build_mark(const KestBuild *build);

// And a number for what was made of them: what the machine will run, rather
// than what was read to get there. Nought for a build that did not compile.
//
// It moves when an instruction, a constant, a name, a promise or a shape that
// crosses the boundary moves, and it does not move when only where they were
// written does — so a program with a comment added, or one run through the
// formatter, has the mark it had. That is what a host caching what it compiled
// asks, and `kest_build_mark` is what a host watching files asks; they are two
// questions and two numbers.
//
// What it costs is that two programs with one mark may say different places
// when they fail: where a chunk came from is not in it. See D659.
uint64_t kest_build_code_mark(const KestBuild *build);

// And all of them added up, which is what a cost is divided by: a program of
// four lines that imports the library costs what the library costs, so a host
// dividing by the file it named would call it fifteen times dearer a byte than
// it is. Nought for no build. See D656.
size_t kest_build_source(const KestBuild *build);

// And what this machine is made of: the stack, the frames, the table of what
// the host provides, and the machine itself. Nought for no machine.
//
// Not what the program has allocated, which is `kest_heap_used` — the two never
// move together. This is what a host pays to start one and gets back when it
// frees one, and it is a machine's own: starting a machine takes nothing from
// the build it was started on, so a host that starts one, frees it and starts
// another pays for one machine rather than for all of them. See D574.
size_t kest_runtime_cost(const KestRuntime *runtime);

// A name the program asks the host for, by position, or NULL past the last of
// them. A host walks from zero until NULL to learn every one.
//
// `kest_start` refuses a program whose externs are not all bound, and says
// which by name. This is the same list before the refusal, for a host that
// embeds a program it did not write and would otherwise learn the names one
// failed start at a time. It is asked of the build, because that is what a
// host has before there is a machine.
const char *kest_build_extern(const KestBuild *build, uint32_t at);

// The three below answer a place past the last one the way they answer a real
// extern that takes nothing or gives nothing back. `K0648` says which, and
// goes to `kest_build_report`: the walk above ends at NULL and asking past
// where it ended is not the same news.

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
