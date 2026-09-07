#ifndef KEST_VALUE_H
#define KEST_VALUE_H

#include "types.h"

// A runtime value carries no tag. The language is statically typed, so the
// instruction knows what it is operating on and an `i32` add is a different
// opcode from an `f32` add. Tagging every value would pay for a question the
// compiler already answered.
typedef union {
    int64_t integer;
    double real;
    bool boolean;
    const char *text;
} KestValue;

typedef enum {
    KEST_OP_CONST,   // u16 index
    KEST_OP_LOAD,    // u16 slot
    KEST_OP_STORE,   // u16 slot
    KEST_OP_TRUE,
    KEST_OP_FALSE,
    KEST_OP_POP,

    KEST_OP_ADD_I,
    KEST_OP_SUB_I,
    KEST_OP_MUL_I,
    KEST_OP_DIV_I,
    KEST_OP_MOD_I,
    KEST_OP_DIV_U,
    KEST_OP_MOD_U,
    KEST_OP_NEG_I,

    KEST_OP_ADD_F,
    KEST_OP_SUB_F,
    KEST_OP_MUL_F,
    KEST_OP_DIV_F,
    KEST_OP_NEG_F,

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

    KEST_OP_NOT,

    KEST_OP_JUMP,        // u16 forward offset
    KEST_OP_JUMP_FALSE,  // u16 forward offset, pops
    KEST_OP_LOOP,        // u16 backward offset

    KEST_OP_CALL,        // u8 function, u8 argument count
    KEST_OP_PRINT,
    KEST_OP_RETURN,
    KEST_OP_RETURN_VOID,
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
    uint8_t param_count;
    uint16_t slot_count;
    bool returns_value;
} KestChunk;

typedef struct {
    KestArena *arena;
    KestChunk **functions;
    uint32_t count;
    uint32_t capacity;
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
uint32_t kest_chunk_constant(KestModule *module, KestChunk *chunk,
                             KestValue value, KestConstClass class);

// Prints every function as instructions, for seeing what the compiler emitted.
void kest_module_disassemble(const KestModule *module, FILE *out);

#endif
