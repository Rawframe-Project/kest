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
    // The address of an element, so a path that reaches through an array can
    // be written to. The address lives for one statement, during which
    // nothing can move what it points at.
    KEST_OP_ELEM_ADDR,  // u16 layout
    KEST_OP_LOAD_AT,    // u16 byte offset, u16 layout
    KEST_OP_STORE_AT,   // u16 byte offset, u16 layout
    KEST_OP_LEN,
    // A piece of text is a pointer and nothing else, so its length is counted
    // rather than read. One byte of it is a `u8`; there is no character type
    // and nothing here pretends to decode one.
    KEST_OP_TEXT_LEN,
    KEST_OP_TEXT_AT,
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
    KEST_OP_CONCAT,     // u16 count
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
    KEST_OP_SEEK,
    KEST_OP_STORE_REF,
    KEST_OP_TRUE,
    KEST_OP_FALSE,
    KEST_OP_POP,
    KEST_OP_POPN,    // u16 count
    KEST_OP_DUP,

    KEST_OP_ADD_I,
    KEST_OP_SUB_I,
    KEST_OP_MUL_I,
    KEST_OP_DIV_I,
    KEST_OP_MOD_I,
    KEST_OP_DIV_U,
    KEST_OP_MOD_U,
    KEST_OP_NEG_I,
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

    KEST_OP_CALL,        // u16 function, u16 argument slots
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
    uint16_t slot_count;
    // How deep the operand stack gets. The compiler knows it exactly, so the
    // machine checks for room once per call instead of once per push.
    uint16_t stack_needed;
    bool returns_value;
} KestChunk;

// A function the program declared and the host must provide.
typedef struct {
    const char *name;
    KestSpan span;
    const KestSource *source;
} KestExtern;

typedef struct {
    KestArena *arena;
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

void kest_module_init(KestModule *module, KestArena *arena);
KestChunk *kest_module_add(KestModule *module, const char *name);
// The index of a function by name, or -1. Calls are resolved through this, so
// a chunk holds an index rather than a pointer and stays copyable.
int32_t kest_module_find(const KestModule *module, const char *name);

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

// What one value of this type is where memory is shared, which is also the
// width its arithmetic is cut to.
uint8_t kest_scalar_of(const KestType *type);

uint32_t kest_chunk_constant(KestModule *module, KestChunk *chunk,
                             KestValue value, KestConstClass class);

// Prints every function as instructions, for seeing what the compiler emitted.
void kest_module_disassemble(const KestModule *module, FILE *out);

#endif
