#ifndef KEST_VALUE_H
#define KEST_VALUE_H

#include "kest.h"
#include "types.h"

typedef enum {
    KEST_OP_CONST,   // u16 index
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
    KestLayout *layouts;
    const KestType **layout_types;
    uint32_t layout_count;
    uint32_t layout_capacity;
} KestModule;

// The least a machine can be given: the deepest run of frames any call can
// make, and the slots those frames take together. False when there is no
// answer, and `why` says which of the two it was and in which function.
bool kest_module_needs(const KestModule *module, KestArena *arena,
                       uint32_t *stack_slots, uint32_t *call_depth,
                       KestReason *why);

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

bool kest_chunk_emit(KestModule *module, KestChunk *chunk, uint8_t byte,
                     uint32_t origin);
bool kest_chunk_emit_u16(KestModule *module, KestChunk *chunk, uint16_t value,
                         uint32_t origin);
// Records a name the host must provide and returns where it sits in the list.
// Declaring the same one twice records it once.
int32_t kest_module_extern(KestModule *module, const char *name, KestSpan span,
                           const KestSource *source);

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

// The shortest spelling that reads back as the same number, so what is
// printed is what is there. A float with nothing after the point still gets
// one, because `3` and `3.0` are not the same value in this language.
int kest_write_real(char *buffer, size_t size, double value, bool narrow);

uint32_t kest_chunk_constant(KestModule *module, KestChunk *chunk,
                             KestValue value, KestConstClass class);

// Prints every function as instructions, for seeing what the compiler emitted.
void kest_module_disassemble(const KestModule *module, FILE *out);

#endif
