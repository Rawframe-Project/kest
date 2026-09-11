#ifndef KEST_VALUE_H
#define KEST_VALUE_H

#include "kest.h"
#include "types.h"

typedef enum {
    KEST_OP_CONST,   // u16 index
    // A run of them, which is what a constant that is a struct or that many of
    // something is: one instruction and one copy rather than a push a slot.
    KEST_OP_CONST_RUN, // u16 first index, u16 count
    // One of a run of them, at an index worked out while running. The run is
    // in the chunk, so nothing is copied into slots to read one of it.
    KEST_OP_CONST_AT,  // u16 first index, u16 stride, u16 how many
    KEST_OP_LOAD,    // u16 slot
    KEST_OP_STORE,   // u16 slot
    // The multi-slot forms. A struct is a value laid out flat, so moving one
    // is moving a run of slots rather than following a pointer.
    KEST_OP_LOADN,   // u16 slot, u16 count
    KEST_OP_STOREN,  // u16 slot, u16 count
    // Keeps one member of the struct on top of the stack and drops the rest.
    // Only needed where the struct is not rooted in a slot, because a field of
    // a local is reached by adding to the slot number instead.
    KEST_OP_FIELD,   // u16 offset, u16 size, u16 total
    // Takes count elements of stride slots each off the stack and leaves a
    // handle in their place.
    // The layout is the module's, and it is what an element is in memory:
    // an array of `f32` is four bytes an element and can be the array the
    // host already has.
    KEST_OP_ARRAY,      // u16 count, u16 layout
    // An array of a size nobody wrote down, and one more element on the end.
    // Growing moves the elements, so a borrowed block cannot be grown and the
    // machine says so rather than writing past what it was lent.
    KEST_OP_MAKE_ARRAY, // u16 layout
    KEST_OP_PUSH,       // u16 layout
    KEST_OP_INDEX,      // u16 layout
    KEST_OP_POP_LAST,   // u16 layout, leaves an optional
    KEST_OP_TAKE,       // u16 layout, shifts what is after it down
    KEST_OP_CLEAR,
    // The address of an element, so a path that reaches through an array can
    // be written to. The address lives for one statement, during which
    // nothing can move what it points at.
    KEST_OP_ELEM_ADDR,  // u16 layout
    // That many of something, laid out where it stands. The index is worked
    // out while running, so these take a base and a stride rather than the
    // single slot `load` and `store` take.
    KEST_OP_LOAD_SLOTS,  // u16 base, u16 stride, u16 count
    KEST_OP_STORE_SLOTS, // u16 base, u16 stride, u16 count
    // The address of one of them inside memory the host laid out.
    KEST_OP_OFFSET_ADDR, // u16 stride, u16 count
    KEST_OP_LOAD_AT,    // u16 byte offset, u16 layout
    KEST_OP_STORE_AT,   // u16 byte offset, u16 layout
    KEST_OP_LEN,
    // A piece of text is a pointer and nothing else, so its length is counted
    // rather than read. One byte of it is a `u8`; there is no character type
    // and nothing here pretends to decode one.
    KEST_OP_TEXT_LEN,
    KEST_OP_TEXT_AT,
    // The byte at a place in text a walk has already measured. It does not
    // look again: text does not change, the walk took its length when it
    // began, and the count it is reading with is the walk's own. `text.at`
    // is what a program's own index compiles to and that one measures.
    KEST_OP_TEXT_IN,     // u16 text slot, u16 index slot
    // A piece of a piece of text is a new one, because a piece of text is a
    // pointer to something that ends in a nought and a window into the middle
    // of one is not that. Finding is only reading and costs nothing.
    KEST_OP_TEXT_SLICE,
    // What is left of a piece of text from a place in it. A piece ends where
    // it ends, so the rest of one is a place inside it and nothing is copied.
    KEST_OP_TEXT_REST,
    // Whether a piece of text sits at a place in another. It compares where
    // it is told rather than looking for it, so what it costs is the place it
    // steps to and the piece it compares, and nothing is copied to do it.
    KEST_OP_TEXT_MATCHES,
    KEST_OP_TEXT_FIND,
    // Text is built rather than found, so each of these reaches the heap and
    // the contract charges for it.
    KEST_OP_TEXT_I,
    KEST_OP_TEXT_U,
    KEST_OP_TEXT_F,
    KEST_OP_TEXT_F32,
    KEST_OP_TEXT_B,
    // u16 layout. The names come from the type the layout was made for, and
    // what is written is the source that builds the value.
    KEST_OP_TEXT_FLAGS,
    KEST_OP_TEXT_ENUM,
    KEST_OP_CONCAT,     // u16 count
    // A number standing for a value, over exactly what `==` applies to.
    KEST_OP_HASH_I,
    KEST_OP_HASH_F,
    KEST_OP_HASH_T,
    // u16 layout. Over the tag and whatever the case carries, which is what
    // the value is and all it is.
    KEST_OP_HASH_ENUM,
    KEST_OP_EQ_ENUM,
    KEST_OP_NE_ENUM,
    KEST_OP_TEXT_FROM,  // an array of bytes becomes one piece of text
    // The slot map. A reference is an index with the generation it was handed
    // out at packed above it, so a read can tell a live one from a stale one
    // without anything having been notified of the removal.
    KEST_OP_NEW_STORE,  // u16 stride
    KEST_OP_ADD,        // u16 stride
    KEST_OP_GET,        // u16 stride, leaves an optional
    KEST_OP_SET,        // u16 stride
    KEST_OP_REMOVE,
    KEST_OP_COUNT,
    // Walking a store. The first live slot at or after one, and the reference
    // that names a slot, kept apart so the loop can hold its place between
    // turns without holding anything the program can see.
    // Finding the next live slot of a store and leaving when there is none.
    // A store's walk cannot count to a limit, because slots go dead; these are
    // to it what the counting instructions are to every other walk.
    KEST_OP_SEEK_FROM,   // u16 store slot, u16 index slot, u16 forward offset
    KEST_OP_SEEK_NEXT,   // u16 store slot, u16 index slot, u16 backward offset
    KEST_OP_STORE_REF,
    KEST_OP_TRUE,
    KEST_OP_FALSE,
    KEST_OP_POP,
    KEST_OP_POPN,    // u16 count
    KEST_OP_DUP,
    // Turns the top run of slots over end to end. A case is built payload
    // first and tag last, because that is the order it is written in, and is
    // laid out tag first, because that is the order it is read in.
    KEST_OP_ROTATE,     // u16 count

    KEST_OP_ADD_I,
    KEST_OP_SUB_I,
    KEST_OP_MUL_I,
    KEST_OP_DIV_I,
    KEST_OP_MOD_I,
    KEST_OP_DIV_U,
    KEST_OP_MOD_U,
    KEST_OP_NEG_I,
    KEST_OP_AND_I,
    KEST_OP_OR_I,
    KEST_OP_XOR_I,
    KEST_OP_NOT_I,
    KEST_OP_SHL,
    KEST_OP_SHR_I,
    KEST_OP_SHR_U,
    // Cuts a result down to the width its type declares. A slot is sixty-four
    // bits and an `i8` is eight, and what the engine on the other side gets
    // is the eight.
    KEST_OP_NARROW,     // u16 scalar kind
    // Between the two families. Nothing crosses on its own, so each of these
    // is somewhere a type was named.
    KEST_OP_I2F,
    KEST_OP_U2F,
    KEST_OP_F2I,        // u16 scalar kind, saturating
    KEST_OP_TO_F32,

    KEST_OP_ADD_F,
    KEST_OP_SUB_F,
    KEST_OP_MUL_F,
    KEST_OP_DIV_F,
    KEST_OP_NEG_F,
    // A slot holds a double, but `f32` arithmetic must round to `f32` or the
    // answer is not the one the engine on the other side of the boundary
    // gets. Rounding the double result is exact for these five.
    KEST_OP_ADD_F32,
    KEST_OP_SUB_F32,
    KEST_OP_MUL_F32,
    KEST_OP_DIV_F32,
    KEST_OP_NEG_F32,

    KEST_OP_LT_I,
    KEST_OP_LE_I,
    KEST_OP_GT_I,
    KEST_OP_GE_I,
    KEST_OP_LT_U,
    KEST_OP_LE_U,
    KEST_OP_GT_U,
    KEST_OP_GE_U,
    KEST_OP_LT_F,
    KEST_OP_LE_F,
    KEST_OP_GT_F,
    KEST_OP_GE_F,

    // Equality is one comparison per storage class, not one per type.
    KEST_OP_EQ_I,
    KEST_OP_NE_I,
    KEST_OP_EQ_F,
    KEST_OP_NE_F,
    KEST_OP_EQ_T,
    KEST_OP_NE_T,
    // Text compares by its bytes, which is an order that is the same
    // everywhere rather than one that depends on where the program is run.
    KEST_OP_LT_T,
    KEST_OP_LE_T,
    KEST_OP_GT_T,
    KEST_OP_GE_T,

    KEST_OP_NOT,

    KEST_OP_JUMP,        // u16 forward offset
    KEST_OP_JUMP_FALSE,  // u16 forward offset, pops
    // A comparison of whole numbers and the jump that reads it, as one
    // instruction: `if a < b` and `while a < b` are what a frame is made of,
    // and both were two dispatches where the second only ever read what the
    // first had just written. The compiler makes these where it emits the
    // jump and nowhere else, so a comparison whose answer is used rather than
    // branched on is still its own instruction.
    // `a || b` asks whether the first one is true, and `!x` asks the same
    // question of one thing, so both were written as `not` and then a jump
    // that reads what `not` wrote. The jump asks it directly.
    KEST_OP_JUMP_TRUE,   // u16 forward offset, pops
    KEST_OP_JUMP_FALSE_LT_I, // u16 forward offset, pops two
    KEST_OP_JUMP_FALSE_LE_I,
    KEST_OP_JUMP_FALSE_GT_I,
    KEST_OP_JUMP_FALSE_GE_I,
    KEST_OP_JUMP_FALSE_EQ_I,
    KEST_OP_JUMP_FALSE_NE_I,
    KEST_OP_JUMP_TRUE_LT_I,
    KEST_OP_JUMP_TRUE_LE_I,
    KEST_OP_JUMP_TRUE_GT_I,
    KEST_OP_JUMP_TRUE_GE_I,
    KEST_OP_JUMP_TRUE_EQ_I,
    KEST_OP_JUMP_TRUE_NE_I,
    KEST_OP_JUMP_FALSE_LT_F,
    KEST_OP_JUMP_FALSE_LE_F,
    KEST_OP_JUMP_FALSE_GT_F,
    KEST_OP_JUMP_FALSE_GE_F,
    KEST_OP_JUMP_FALSE_EQ_F,
    KEST_OP_JUMP_FALSE_NE_F,
    KEST_OP_JUMP_TRUE_LT_F,
    KEST_OP_JUMP_TRUE_LE_F,
    KEST_OP_JUMP_TRUE_GT_F,
    KEST_OP_JUMP_TRUE_GE_F,
    KEST_OP_JUMP_TRUE_EQ_F,
    KEST_OP_JUMP_TRUE_NE_F,
    KEST_OP_LOOP,        // u16 backward offset
    // The whole of a counted walk's turn: add one to the count, compare it
    // with the limit beside it, and go back while it is less. The test is at
    // the bottom and the one before the first turn is written above the loop,
    // so a turn costs one instruction rather than five.
    KEST_OP_NEXT_LESS_I, // u16 slot, u16 limit slot, u16 backward offset
    KEST_OP_NEXT_LESS_U, // u16 slot, u16 limit slot, u16 backward offset

    KEST_OP_CALL,        // u16 function, u16 argument slots
    // Through a value rather than a name. Which function it is sits on top of
    // the arguments; what it promises is in its type, so a cost contract is
    // still proved without knowing which one it will be.
    KEST_OP_CALL_VALUE,  // u16 argument slots
    // Into the host. The index is into the module's list of what it declared,
    // which is resolved by name before the program runs.
    KEST_OP_CALL_HOST,   // u16 extern, u16 argument slots, u16 result slots
    KEST_OP_RETURN,  // u16 count
} KestOp;

// What a constant's bits mean. The virtual machine never reads this; it is
// what lets the disassembler print a constant rather than its bits.
typedef enum {
    KEST_CONST_INT,
    KEST_CONST_FLOAT,
    KEST_CONST_TEXT,
} KestConstClass;

typedef struct {
    const char *name;
    // The file this was compiled from, so a failure while running reports in
    // the same place a failure to compile would have.
    const KestSource *source;
    uint8_t *code;
    uint32_t code_count;
    uint32_t code_capacity;
    // The source offset each instruction came from, so a runtime failure can
    // be reported where a compile failure would have been.
    uint32_t *origins;
    KestValue *constants;
    uint8_t *constant_classes;
    uint32_t constant_count;
    uint32_t constant_capacity;
    // In slots, not in names: a struct parameter is a run of them.
    uint16_t param_slots;
    // What each of them is, in the order they are written: an index into the
    // module's layouts, which says both how wide the argument is and what is
    // in it. A host asks where an argument starts rather than counting the
    // scalars of the ones before it, and asks what it is rather than trusting
    // that its own idea of the type is the program's.
    uint16_t *takes;
    uint16_t takes_count;
    // And what comes back, the same way: an index into the module's layouts,
    // read only when the function gives something.
    uint16_t gives;
    // What it gives back, so a host can be told how wide a frame has to be
    // without the types being around to ask.
    uint16_t result_slots;
    uint16_t slot_count;
    // How deep the operand stack gets. The compiler knows it exactly, so the
    // machine checks for room once per call instead of once per push.
    uint16_t stack_needed;
    bool returns_value;
    // What the declaration promised. The promise is checked against the tree
    // before anything is emitted; this is what lets it be checked again
    // against what was emitted. See D058.
    bool no_alloc;
} KestChunk;

// A function the program declared and the host must provide.
typedef struct {
    const char *name;
    KestSpan span;
    const KestSource *source;
    // What the program expects it to take and to give back, the same way a
    // chunk says it: an index into the module's layouts for each argument in
    // the order they are written, and one for the answer. A host binds a C
    // function and nothing else checks that the two agree about what crosses.
    uint16_t *takes;
    uint16_t takes_count;
    uint16_t gives;
    bool gives_value;
    // What the program was told about the heap. An extern declared `no.alloc`
    // is a promise made on the host's behalf by whoever wrote the declaration,
    // and it is the one promise in this language that the machine has to hold
    // somebody else to.
    bool promises;
} KestExtern;

typedef struct {
    KestArena *arena;
    // What the file that was named calls itself. A host writes `spawn` and
    // the program registered `world.spawn`, and this is what tells them apart
    // without the host having to know there was a difference.
    const char *alias;
    KestChunk **functions;
    uint32_t count;
    uint32_t capacity;
    KestExtern *externs;
    uint32_t extern_count;
    uint32_t extern_capacity;
    // What the next place handed out in a store is stamped with. It is here
    // rather than in a machine because two machines from one build are two
    // worlds of one program, and a reference from one of them handed to the
    // other would otherwise name whatever is standing in that place: both
    // would have started counting at one. See D316.
    uint32_t stamps;
    // How many machines are standing on this program. Freeing the build takes
    // the program out from under every one of them, so the build is refused
    // while any of them is still there. It is here for the same reason the
    // stamps are: what the machines have in common is the build, and this is
    // the part of it they all touch. See D324.
    uint32_t machines;
    KestLayout *layouts;
    const KestType **layout_types;
    uint32_t layout_count;
    uint32_t layout_capacity;
} KestModule;

// The least a machine can be given: the deepest run of frames any call can
// make, and the slots those frames take together. False when there is no
// answer, and `why` says which of the two it was and in which function.
//
// `only` is which function to answer for, or -1 for every one of them, which
// is what a host that has not said which it calls has to be given.
//
// `from_host_slots` and `from_host_frames` are where the machine is at the
// deepest place it calls into the host, which is where a host function that
// calls back in starts from. Both are nought when nothing reaches a host
// function, and either may be NULL for a caller that is not asking.
// What one function needs, and why it has none where it has none. A machine
// that will only ever be called at one function needs what that function
// reaches rather than what the worst of them does, and the walk that answers
// for the whole program works out both on the way: `slots` and `frames` are
// what a machine to call this one takes. `reach` is nought where there is an
// answer, and where it is not, `from` is the function it came from — a caller
// of a function with no answer has none either, and what a reader wants is the
// one with the `call.value` or the loop in it. See D602 and D603.
typedef struct {
    uint32_t slots;
    uint32_t frames;
    uint32_t from;
    uint8_t reach;
} KestNoLeast;

// One walk of the whole program, kept. A module does not change after it is
// compiled, so the answer does not either: what it needs, where it calls into
// the host and which function that is. A host asks for it and every machine
// asks for it again, and working it out costs more scratch than a machine is
// made of. See D607.
typedef struct {
    bool taken;
    bool measured;
    uint32_t slots;
    uint32_t frames;
    uint32_t host_slots;
    uint32_t host_frames;
    KestReason why;
} KestWalk;

bool kest_module_needs(const KestModule *module, KestArena *arena, int32_t only,
                       uint32_t *stack_slots, uint32_t *call_depth,
                       uint32_t *from_host_slots, uint32_t *from_host_frames,
                       KestNoLeast *reasons, KestReason *why);

// Holds every `no.alloc` promise against the code that was emitted for it,
// rather than against the tree it was checked on. Reports what it finds and
// returns false when it found anything.
bool kest_module_prove(const KestModule *module, KestArena *arena,
                       KestDiags *diags);

void kest_module_init(KestModule *module, KestArena *arena);
KestChunk *kest_module_add(KestModule *module, const char *name);
// The index of a function by name, or -1. Calls are resolved through this, so
// a chunk holds an index rather than a pointer and stays copyable.
int32_t kest_module_find(const KestModule *module, const char *name);
// How many functions a generic name stands for, filling `found` with the
// first `room` of them. A generic is compiled once per set of types and each
// copy is named `sort#i32`, which is one place in the program and is not a
// name anybody wrote; this is that place, so that looking a name up and
// saying why the lookup could not answer agree about what a copy is.
uint32_t kest_module_copies(const KestModule *module, const char *name,
                            int32_t *found, uint32_t room);
// The name a program writes, out of the one a function was compiled under.
// What a function takes is part of what makes it that function rather than
// another one, so a copy is named `sort#i32`; nobody wrote that, and anything
// said to a person stops at the hash.
const char *kest_name_written(KestArena *arena, const char *symbol);

// Which function a host means by a name: the name as written, and then the
// same name under the module of the file that was named. -1 for one the
// program does not have.
int32_t kest_module_entry(const KestModule *module, const char *name);

bool kest_chunk_emit(KestModule *module, KestChunk *chunk, uint8_t byte,
                     uint32_t origin);
bool kest_chunk_emit_u16(KestModule *module, KestChunk *chunk, uint16_t value,
                         uint32_t origin);
// Records a name the host must provide and returns where it sits in the list.
// Declaring the same one twice records it once.
int32_t kest_module_extern(KestModule *module, const char *name, KestSpan span,
                           const KestSource *source, bool promises);
// What the program expects the extern at `at` to take and give. The layouts
// are the caller's to work out, because working one out is the compiler's job
// and this file is where they are kept.
void kest_module_extern_shape(KestModule *module, uint32_t at, uint16_t *takes,
                              uint16_t count, uint16_t gives,
                              bool gives_value);

// The layout of a type, built once and shared. Returns where it sits in the
// module's table.
int32_t kest_module_layout(KestModule *module, const KestType *type);
// How many types of a written name the program lays out, filling `layout` when
// there is exactly one. Nought is a name the program does not hold in an
// array, which is the same as one it cannot lend, and more than one is a name
// that needs the module written in front of it. A lend and a host asking what
// it will be lending ask this, so the two cannot come apart about either.
uint32_t kest_module_layout_of(const KestModule *module, const char *name,
                               const KestLayout **found, uint32_t room);
// The nearest name that can be lent to, or NULL when nothing is near enough.
// Only what the program holds in an array is offered, because a name it has
// and cannot lend is a suggestion that fails the same way.
const char *kest_module_nearest(const KestModule *module, const char *name);
// The name a host would have to write to get one type back, which is the one
// given when that means a single type and the whole of the other when it does
// not. NULL when nothing of that name can be asked for at all.
const char *kest_module_askable(const KestModule *module, const char *name);

// What one value of this type is where memory is shared, which is also the
// width its arithmetic is cut to.
uint8_t kest_scalar_of(const KestType *type);

// A value written the way a program writes one, read back out of a word: what
// a shell hands the command line and what a host hands over rather than laying
// out slots itself. Anything that is not a number, a truth or a piece of text
// cannot be written as a word, and saying so beats guessing.
//
// False when the word is not one of that type, and `why` is what to say about
// it — a reason without the word or the type in front of it, so that a caller
// says where it came from in its own words.
bool kest_value_read(KestArena *arena, const char *text, const KestType *type,
                     KestValue *into, const char **why);

// What a piece of a layout is called, which is the name a program writes for
// that type. Anything that is not one of them is said as such rather than read
// past the end of the list.
const char *kest_scalar_name(uint8_t kind);

// And what a reason there is no least is called, which the JSON, the words a
// listing prints and the machine refusing for want of stack are the same list
// of: a reason added to `KestReach` is caught here rather than printed as
// whatever the last one fell through to.
const char *kest_reach_name(KestReach reach);

// The shortest spelling that reads back as the same number, so what is
// printed is what is there. A float with nothing after the point still gets
// one, because `3` and `3.0` are not the same value in this language.
int kest_write_real(char *buffer, size_t size, double value, bool narrow);

uint32_t kest_chunk_constant(KestModule *module, KestChunk *chunk,
                             KestValue value, KestConstClass class);
// The same for a run of them, kept together and in order because what reads
// them back is one copy. Gives where the run starts.
uint32_t kest_chunk_constant_run(KestModule *module, KestChunk *chunk,
                                 const KestValue *values,
                                 const uint8_t *classes, uint32_t count);

// Prints every function as instructions, for seeing what the compiler emitted.
// The instructions, for a person. `entries` is a NULL-terminated list of the
// names whose own cost is worth printing beside the program's, which is the
// caller's to say: a library does not know which functions a host will call.
void kest_module_disassemble(const KestModule *module,
                             const char *const *entries, FILE *out);
// The same thing for whatever is reading it rather than for a person: what is
// laid out, what the host must provide, and every function with its
// instructions as an offset, a name and the numbers after it. What the text
// form decorates — the value behind a constant, where a jump lands — is left
// as the numbers, because a reader that wanted prose would have asked for it.
// What one question about room answers, written as the fields of an object
// without the braces around them: the whole program when `only` is -1 and one
// function when it is not. Every part of this project that says this in JSON
// says it through here, so the shape a tool reads for a program and the shape
// it reads for a function are the same shape, nulls and all.
void kest_module_needs_json(const KestModule *module, int32_t only, FILE *out);

// The same, as one object. `entries` is the names to answer about beside the
// program, and every one of them the program has is in the answer whether or
// not it differs — a tool looks one up rather than reading a list.
void kest_module_disassemble_json(const KestModule *module,
                                  const char *const *entries, FILE *out);

#endif
