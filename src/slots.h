#ifndef KEST_SLOTS_H
#define KEST_SLOTS_H

#include "ir.h"

// The second backend, and the experiment section 5 of the continuation asks
// for: the same bodies written for a machine that names where every value is
// instead of pushing it.
//
// A frame here is one run of slots: the names a body declared, and above them
// one place for every value the body makes. Where a value goes is the depth at
// which it was made, which is what `kest_ir_windows` answers — so nothing here
// allocates registers, because a body's values are made and read in the order a
// tree walk makes them and that order already gives each of them a place.
//
// What it is for is a number. The stack backend spends most of what it runs
// moving values onto the stack and off it; this one reads them where they are.
// D958 counted what that would save and D961 measured what a removed push is
// worth in time. This is the third thing: a machine that actually does it.
//
// What it covers is what the workloads it is measured over are made of, and it
// refuses a body it cannot write rather than writing one that means something
// else. A program is run by this machine only when every body it can reach has
// been written for it.

// An operand is one byte: a place in the frame, or a value the chunk holds when
// the top bit is set. A stack machine spends an instruction pushing a constant;
// this reads it where it is, which is most of what the difference between the
// two comes to.
//
// One byte rather than two because what a three-address form costs is reading
// its operands, and a body with more than a hundred and twenty-seven places in
// it is not one this machine is for: it refuses those rather than paying for
// them everywhere. That is what "narrow" means in the mission's own words for
// what this is.
#define KEST_SLOT_HELD 0x80u
#define KEST_SLOT_MOST 0x80u

typedef enum {
    // d, a, how many
    KEST_SLOT_MOVE,
    // d, the array, which one, the layout
    KEST_SLOT_ELEM,
    // the array, which one, what, the layout
    KEST_SLOT_SET_ELEM,
    KEST_SLOT_LEN,
    // d, a, how far in, how many
    KEST_SLOT_PART,
    KEST_SLOT_ADD_I,
    KEST_SLOT_SUB_I,
    KEST_SLOT_MUL_I,
    KEST_SLOT_DIV_I,
    KEST_SLOT_MOD_I,
    KEST_SLOT_DIV_U,
    KEST_SLOT_MOD_U,
    KEST_SLOT_NEG_I,
    KEST_SLOT_AND_I,
    KEST_SLOT_OR_I,
    KEST_SLOT_XOR_I,
    KEST_SLOT_SHL,
    KEST_SLOT_SHR_I,
    KEST_SLOT_SHR_U,
    KEST_SLOT_ADD_F,
    KEST_SLOT_SUB_F,
    KEST_SLOT_MUL_F,
    KEST_SLOT_DIV_F,
    KEST_SLOT_NEG_F,
    KEST_SLOT_ADD_F32,
    KEST_SLOT_SUB_F32,
    KEST_SLOT_MUL_F32,
    KEST_SLOT_DIV_F32,
    KEST_SLOT_NEG_F32,
    // d, a, which width
    KEST_SLOT_NARROW,
    KEST_SLOT_LT_I,
    KEST_SLOT_LE_I,
    KEST_SLOT_GT_I,
    KEST_SLOT_GE_I,
    KEST_SLOT_EQ_I,
    KEST_SLOT_NE_I,
    KEST_SLOT_LT_U,
    KEST_SLOT_LE_U,
    KEST_SLOT_GT_U,
    KEST_SLOT_GE_U,
    KEST_SLOT_LT_F,
    KEST_SLOT_LE_F,
    KEST_SLOT_GT_F,
    KEST_SLOT_GE_F,
    KEST_SLOT_EQ_F,
    KEST_SLOT_NE_F,
    KEST_SLOT_NOT,
    KEST_SLOT_TRUE,
    KEST_SLOT_FALSE,
    // where to go, as the byte this body's code starts counting from
    KEST_SLOT_GO,
    // what to ask, where to go
    KEST_SLOT_GO_FALSE,
    KEST_SLOT_GO_TRUE,
    // the count, the limit, where to go
    KEST_SLOT_NEXT_LESS_I,
    KEST_SLOT_NEXT_LESS_U,
    // d, which function, where its arguments start, how many slots they are
    KEST_SLOT_CALL,
    // a, how many
    KEST_SLOT_GIVE,
    KEST_SLOT_OP_COUNT
} KestSlotOp;

typedef struct {
    uint8_t *code;
    uint32_t code_count;
    uint32_t code_capacity;
    // Which functions this body calls, so that what says a program can be run
    // by this machine is what the entry reaches rather than the whole module:
    // a body nothing calls need not have been written for it.
    uint16_t *calls;
    uint32_t call_count;
    uint32_t call_capacity;
    // How wide the frame is: the names, then a place for every value.
    uint16_t frame_slots;
    // How many instructions it came to, which is the number the experiment is
    // about: the first machine's count for the same body is `origin_count` on
    // its chunk.
    uint32_t instructions;
    // Whether this body was written for this machine at all.
    bool written;
} KestSlotBody;

typedef struct {
    KestArena *arena;
    KestModule *module;
    KestProgram *program;
    KestSlotBody *bodies;
    uint32_t count;
    uint32_t capacity;
    uint32_t next;
    // How many bodies this backend could not write, and the first reason.
    uint32_t refused;
    const char *why;
    bool out_of_memory;
} KestSlotWriter;

// How many instructions each machine has run since the process began, in the
// build that checks itself and nowhere else. The experiment wants the count as
// well as the duration, and counting in the build that is already paying for
// that sort of thing is how everything else here is counted. See D330.
void kest_slots_counted(uint64_t *by_the_stack, uint64_t *by_the_slots);

KestSlotWriter *kest_slots_new(KestProgram *program, KestModule *module,
                               KestArena *arena);
bool kest_slots_body(void *writer, const KestIrBody *body);
// Whether this entry and everything it calls were written for this machine,
// which is what says a call in can be run by it.
bool kest_slots_reaches(const KestSlotWriter *writer, uint32_t entry);
const KestSlotBody *kest_slots_of(const KestSlotWriter *writer, uint32_t at);

#endif
