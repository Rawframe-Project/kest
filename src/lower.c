#include "lower.h"

#include <stdlib.h>
#include <string.h>

// A jump and a loop carry how far as two bytes, so this is how much code there
// can be between one and where it lands.
#define MAX_REACH UINT16_MAX

struct KestLower {
    KestProgram *program;
    KestModule *module;
    KestArena *arena;
    // Which chunk comes next. Bodies arrive in the order the module's
    // functions were registered in, which is what makes this a count rather
    // than a search.
    uint32_t next;
    KestChunk *chunk;
    const KestIrBody *body;

    // Where each operation's first byte is, so that a branch can be filled in
    // once its landing place has been written.
    uint32_t *at;
    // Whether anything branches to each operation. Two instructions written
    // one after the other can be made into one, and that moves where the
    // second of them starts — so a jump landing between them would land inside
    // an instruction. Nothing at a place something points at may be taken back
    // into what comes after it. See D871.
    bool *landed_on;
    // The branches written and not yet filled in: where the two bytes are, and
    // which operation they lead to.
    uint32_t *waiting;
    uint32_t *leads_to;
    uint32_t wait_count;

    // The last two instructions written and where each starts, so that a jump
    // can take the `not` before it and the comparison before that into itself.
    // Two, because that is as far back as anything reaches.
    uint8_t last_op;
    uint32_t last_at;
    uint8_t before_op;
    uint32_t before_at;
    uint32_t pointed_at;

    bool out_of_memory;
};

typedef struct KestLower Lower;

static void refuse(Lower *lower, KestSpan span, const char *code,
                   const char *format, ...) {
    va_list args;
    va_start(args, format);
    kest_diags_addv(lower->program->diags, KEST_SEVERITY_ERROR, code, span,
                    format, args);
    va_end(args);
}

// An operation with no instruction behind it. Reaching one means the body and
// this backend disagree about what a program is, which is this project's
// mistake and not the program's — so it says so, in the words the other half
// of the compiler uses for the same kind of news.
static void fault(Lower *lower, KestSpan span, const char *what) {
    kest_diags_disagree(lower->program->diags, span, "%s", what);
}

static void emit(Lower *lower, uint8_t byte, KestSpan origin) {
    lower->before_op = lower->last_op;
    lower->before_at = lower->last_at;
    lower->last_op = byte;
    lower->last_at = lower->chunk->code_count;
    if (!kest_chunk_emit(lower->module, lower->chunk, byte, origin.offset)) {
        lower->out_of_memory = true;
    }
}

static void emit_u16(Lower *lower, uint16_t value, KestSpan origin) {
    if (!kest_chunk_emit_u16(lower->module, lower->chunk, value,
                             origin.offset)) {
        lower->out_of_memory = true;
    }
}

static void take_back(Lower *lower) {
    kest_chunk_take_back(lower->chunk, lower->last_at);
    lower->last_op = lower->before_op;
    lower->last_at = lower->before_at;
}

// Whether the last thing written was a load of one slot, and which. It is the
// same question `load_before` asks and a narrower one: a run of slots cannot be
// the first half of either pair below, because what follows it is not where it
// ends. See D961.
static bool one_load_before(const Lower *lower, uint16_t *slot) {
    if (lower->last_op != KEST_OP_LOAD || lower->last_at < lower->pointed_at ||
        lower->last_at + 3 != lower->chunk->code_count) {
        return false;
    }
    const uint8_t *at = lower->chunk->code + lower->last_at;
    *slot = (uint16_t)(at[1] | ((uint16_t)at[2] << 8));
    return true;
}

// What the load just written reads, when the last thing written was a load and
// nothing points between the two. Answers how many slots it took and where they
// start, or nought for anything else.
static uint16_t load_before(const Lower *lower, uint16_t *slot) {
    uint32_t width = lower->last_op == KEST_OP_LOAD    ? 3
                     : lower->last_op == KEST_OP_LOADN ? 5
                                                       : 0;
    if (width == 0 || lower->last_at < lower->pointed_at ||
        lower->last_at + width != lower->chunk->code_count) {
        return 0;
    }
    const uint8_t *at = lower->chunk->code + lower->last_at;
    *slot = (uint16_t)(at[1] | ((uint16_t)at[2] << 8));
    return (uint16_t)(width == 3 ? 1 : (at[3] | ((uint16_t)at[4] << 8)));
}

// Two loads of slots that sit next to each other are one load of both. A struct
// built out of locals is written as a load for each field, and the machine
// already has an instruction that takes a run of slots in one go — `load.n`,
// which a wide value is loaded with — so this is a dispatch off every field
// past the first and no instruction the machine did not have. See D871.
static void emit_load(Lower *lower, uint16_t slot, uint16_t size,
                      KestSpan origin) {
    uint16_t before = 0;
    uint16_t took = load_before(lower, &before);
    if (took > 0 && (uint32_t)before + took == slot &&
        (uint32_t)took + size <= UINT16_MAX) {
        take_back(lower);
        slot = before;
        size = (uint16_t)(took + size);
    }
    // And two that do not sit next to each other are still two pushes, which
    // is one instruction with two operands. The pair is a tenth of what the
    // frame step runs. See D961.
    uint16_t first = 0;
    if (size == 1 && one_load_before(lower, &first)) {
        take_back(lower);
        emit(lower, KEST_OP_LOAD2, origin);
        emit_u16(lower, first, origin);
        emit_u16(lower, slot, origin);
        return;
    }
    emit(lower, size == 1 ? KEST_OP_LOAD : KEST_OP_LOADN, origin);
    emit_u16(lower, slot, origin);
    if (size != 1) {
        emit_u16(lower, size, origin);
    }
}

static bool fusing(void);

// Whether the last thing written was an index of a run whose element is this
// many slots wide, and which layout it read. An index pushes a struct onto the
// stack and the store after it copies it off again, and the store cannot begin
// until the index has finished: that is the dependency D1011 says is worth
// taking into one instruction. Three bytes, the same shape every other
// peephole here reads.
static bool index_before(const Lower *lower, uint16_t size, uint16_t *layout) {
    if (lower->last_op != KEST_OP_INDEX || lower->last_at < lower->pointed_at ||
        lower->last_at + 3 != lower->chunk->code_count) {
        return false;
    }
    const uint8_t *at = lower->chunk->code + lower->last_at;
    uint16_t which = (uint16_t)(at[1] | ((uint16_t)at[2] << 8));
    if (which >= lower->module->layout_count ||
        lower->module->layouts[which].slots != size) {
        return false;
    }
    *layout = which;
    return true;
}

// Whether the last thing written was the address of one of an array. A read of
// a field through that address is the same read the machine can do from the
// array and the index, in one instruction and one bounds check rather than
// two dispatches and an address round trip. See D1044.
static bool elem_addr_before(const Lower *lower) {
    return lower->last_op == KEST_OP_ELEM_ADDR &&
           lower->last_at >= lower->pointed_at &&
           lower->last_at + 3 == lower->chunk->code_count;
}

// The arithmetic the store just after it is taking the answer of, when that
// arithmetic is the instruction before. Four of them: the two that carry a
// width and the two that do not, which is what the pair counts say the
// programs here run. The width is read back out of the bytes so that the
// fused instruction carries it too. See D1014.
static uint8_t arithmetic_before(const Lower *lower, uint16_t *kind) {
    if (lower->last_at < lower->pointed_at) {
        return 0;
    }
    uint32_t wide = lower->chunk->code_count - lower->last_at;
    const uint8_t *at = lower->chunk->code + lower->last_at;
    if (wide == 1) {
        if (lower->last_op == KEST_OP_ADD_F) {
            return KEST_OP_ADD_F_TO;
        }
        if (lower->last_op == KEST_OP_SUB_F) {
            return KEST_OP_SUB_F_TO;
        }
        return 0;
    }
    if (wide != 3) {
        return 0;
    }
    *kind = (uint16_t)(at[1] | ((uint16_t)at[2] << 8));
    if (lower->last_op == KEST_OP_ADD_I_NARROW) {
        return KEST_OP_ADD_I_NARROW_TO;
    }
    if (lower->last_op == KEST_OP_SUB_I_NARROW) {
        return KEST_OP_SUB_I_NARROW_TO;
    }
    return 0;
}

static void emit_store(Lower *lower, uint16_t slot, uint16_t size,
                       KestSpan origin) {
    uint16_t layout = 0;
    if (size != 1 && fusing() && index_before(lower, size, &layout)) {
        take_back(lower);
        emit(lower, KEST_OP_INDEX_TO, origin);
        emit_u16(lower, layout, origin);
        emit_u16(lower, slot, origin);
        if (size > lower->chunk->fused_slots) {
            lower->chunk->fused_slots = size;
        }
        return;
    }
    uint16_t kind = 0;
    uint8_t made = size != 1 || !fusing() ? 0 : arithmetic_before(lower, &kind);
    if (made != 0) {
        take_back(lower);
        emit(lower, made, origin);
        if (made == KEST_OP_ADD_I_NARROW_TO || made == KEST_OP_SUB_I_NARROW_TO) {
            emit_u16(lower, kind, origin);
        }
        emit_u16(lower, slot, origin);
        return;
    }
    emit(lower, size == 1 ? KEST_OP_STORE : KEST_OP_STOREN, origin);
    emit_u16(lower, slot, origin);
    if (size != 1) {
        emit_u16(lower, size, origin);
    }
}

// Whether the fusions this file makes are made at all. There is one way to
// turn them off and it is here rather than on the command line: what it is for
// is compiling the same program twice and requiring the same answer, which is
// how a transformation is held to being one that keeps a program's meaning.
// D1009 says every optimization is held that way and D1011 is the first one
// that was.
//
// A plainer program runs the same operations in the same order; what differs
// is how many instructions they are written as. Nothing a reader sees changes
// except `kest emit`, which prints what was emitted and is where the
// difference is meant to show.
//
// Read once, because a compiler that asked the environment per body would be
// one whose answer could change half way through a program.
static bool fusing(void) {
    static int decided = -1;
    return !kest_ir_asked_off("KEST_PLAIN", &decided);
}

// A run of values the chunk holds, and the instruction that reads it. A local
// and then a constant is the commonest pair this machine runs — every `x + 1`,
// every `i < n` against a written number — and it is one instruction with two
// operands. See D961.
static void emit_constant(Lower *lower, uint16_t first, uint16_t count,
                          KestSpan origin) {
    const KestIrBody *body = lower->body;
    if ((uint32_t)first + count > body->constant_count) {
        fault(lower, origin, "this reads a value the body has not got");
        return;
    }
    uint32_t index = kest_chunk_constant_run(
        lower->module, lower->chunk, body->constants + first,
        body->constant_classes + first, count);
    if (lower->module->out_of_room) {
        lower->out_of_memory = true;
        return;
    }
    uint16_t slot = 0;
    if (fusing() && count == 1 && index <= UINT16_MAX &&
        one_load_before(lower, &slot)) {
        take_back(lower);
        emit(lower, KEST_OP_LOADK, origin);
        emit_u16(lower, slot, origin);
        emit_u16(lower, (uint16_t)index, origin);
        return;
    }
    emit(lower, count == 1 ? KEST_OP_CONST : KEST_OP_CONST_RUN, origin);
    emit_u16(lower, (uint16_t)index, origin);
    if (count != 1) {
        emit_u16(lower, count, origin);
    }
}

// The arithmetic a cut arrives behind, when it is the instruction just before
// it. Each of the three is one byte and carries nothing after it, so it is the
// last instruction when it is the last byte — the same thing that lets a jump
// take back the comparison before it. See D868.
static uint8_t fused_with_narrow(uint8_t arithmetic) {
    switch (arithmetic) {
    case KEST_OP_ADD_I:
        return KEST_OP_ADD_I_NARROW;
    case KEST_OP_SUB_I:
        return KEST_OP_SUB_I_NARROW;
    case KEST_OP_MUL_I:
        return KEST_OP_MUL_I_NARROW;
    default:
        return KEST_OP_NARROW;
    }
}

// The comparison a jump reads, when the jump is the next thing after it. Every
// one of these leaves its answer on the stack for one instruction, which then
// pops it and throws it away, so the pair is one instruction and one dispatch.
// Only whole numbers and floats: they are what a loop counts with and what an
// index is.
// `a || b` asks whether the first one is true, and `!x` asks the same question
// of one thing, so both were written as `not` and then a jump that reads what
// `not` wrote. The jump asks it directly.
static uint8_t fused_with_jump(uint8_t compare, bool asking_true) {
    switch (compare) {
    case KEST_OP_LT_I:
        return asking_true ? KEST_OP_JUMP_TRUE_LT_I : KEST_OP_JUMP_FALSE_LT_I;
    case KEST_OP_LE_I:
        return asking_true ? KEST_OP_JUMP_TRUE_LE_I : KEST_OP_JUMP_FALSE_LE_I;
    case KEST_OP_GT_I:
        return asking_true ? KEST_OP_JUMP_TRUE_GT_I : KEST_OP_JUMP_FALSE_GT_I;
    case KEST_OP_GE_I:
        return asking_true ? KEST_OP_JUMP_TRUE_GE_I : KEST_OP_JUMP_FALSE_GE_I;
    case KEST_OP_EQ_I:
        return asking_true ? KEST_OP_JUMP_TRUE_EQ_I : KEST_OP_JUMP_FALSE_EQ_I;
    case KEST_OP_NE_I:
        return asking_true ? KEST_OP_JUMP_TRUE_NE_I : KEST_OP_JUMP_FALSE_NE_I;
    case KEST_OP_LT_F:
        return asking_true ? KEST_OP_JUMP_TRUE_LT_F : KEST_OP_JUMP_FALSE_LT_F;
    case KEST_OP_LE_F:
        return asking_true ? KEST_OP_JUMP_TRUE_LE_F : KEST_OP_JUMP_FALSE_LE_F;
    case KEST_OP_GT_F:
        return asking_true ? KEST_OP_JUMP_TRUE_GT_F : KEST_OP_JUMP_FALSE_GT_F;
    case KEST_OP_GE_F:
        return asking_true ? KEST_OP_JUMP_TRUE_GE_F : KEST_OP_JUMP_FALSE_GE_F;
    case KEST_OP_EQ_F:
        return asking_true ? KEST_OP_JUMP_TRUE_EQ_F : KEST_OP_JUMP_FALSE_EQ_F;
    case KEST_OP_NE_F:
        return asking_true ? KEST_OP_JUMP_TRUE_NE_F : KEST_OP_JUMP_FALSE_NE_F;
    default:
        return asking_true ? KEST_OP_JUMP_TRUE : KEST_OP_JUMP_FALSE;
    }
}

// Which instruction compares, by what is being compared. One row an operation,
// because the question is the same four every time — a piece of text, a float,
// an unsigned number, or the plain one — and it was written out six times.
// `==` and `!=` over a run of slots are the exception and are answered where
// they are written: both sides are a run rather than one.
static const struct {
    uint8_t operation;
    uint8_t whole;
    uint8_t without_sign;
    uint8_t real;
    uint8_t text;
} COMPARISONS[] = {
    {KEST_IR_LT, KEST_OP_LT_I, KEST_OP_LT_U, KEST_OP_LT_F, KEST_OP_LT_T},
    {KEST_IR_LE, KEST_OP_LE_I, KEST_OP_LE_U, KEST_OP_LE_F, KEST_OP_LE_T},
    {KEST_IR_GT, KEST_OP_GT_I, KEST_OP_GT_U, KEST_OP_GT_F, KEST_OP_GT_T},
    {KEST_IR_GE, KEST_OP_GE_I, KEST_OP_GE_U, KEST_OP_GE_F, KEST_OP_GE_T},
    // Equality does not ask whether a number has a sign: the same bits are
    // the same bits either way.
    {KEST_IR_EQ, KEST_OP_EQ_I, KEST_OP_EQ_I, KEST_OP_EQ_F, KEST_OP_EQ_T},
    {KEST_IR_NE, KEST_OP_NE_I, KEST_OP_NE_I, KEST_OP_NE_F, KEST_OP_NE_T},
};

static bool is_float(const KestType *type) {
    return type != NULL && type->tag == KEST_T_FLOAT;
}

// Whether a value of this type is a run of slots rather than one, which is
// what says a comparison walks it and a hash walks it.
static bool is_a_run(const KestType *type) {
    return type != NULL &&
           (type->tag == KEST_T_ENUM || type->tag == KEST_T_STRUCT ||
            type->tag == KEST_T_FIXED);
}

static uint8_t compares(uint16_t operation, const KestType *type) {
    bool text = type != NULL && type->tag == KEST_T_TEXT;
    bool real = is_float(type);
    bool without_sign = kest_is_unsigned(type);
    for (size_t i = 0; i < sizeof(COMPARISONS) / sizeof(COMPARISONS[0]); i++) {
        if (COMPARISONS[i].operation != operation) {
            continue;
        }
        return text ? COMPARISONS[i].text
               : real ? COMPARISONS[i].real
               : without_sign ? COMPARISONS[i].without_sign
                              : COMPARISONS[i].whole;
    }
    return 0;
}

// The arithmetic for an operation and what it is on. Which of the three
// families it belongs to is the type's to say, which is why the body carries
// one addition and this carries three.
static uint8_t arithmetic(uint16_t operation, const KestType *type) {
    bool real = is_float(type);
    bool narrow = kest_is_narrow(type);
    bool without_sign = kest_is_unsigned(type);
    switch (operation) {
    case KEST_IR_ADD:
        return real ? (narrow ? KEST_OP_ADD_F32 : KEST_OP_ADD_F)
                    : KEST_OP_ADD_I;
    case KEST_IR_SUB:
        return real ? (narrow ? KEST_OP_SUB_F32 : KEST_OP_SUB_F)
                    : KEST_OP_SUB_I;
    case KEST_IR_MUL:
        return real ? (narrow ? KEST_OP_MUL_F32 : KEST_OP_MUL_F)
                    : KEST_OP_MUL_I;
    case KEST_IR_DIV:
        return real ? (narrow ? KEST_OP_DIV_F32 : KEST_OP_DIV_F)
                    : (without_sign ? KEST_OP_DIV_U : KEST_OP_DIV_I);
    case KEST_IR_MOD:
        return real ? (narrow ? KEST_OP_MOD_F32 : KEST_OP_MOD_F)
                    : (without_sign ? KEST_OP_MOD_U : KEST_OP_MOD_I);
    case KEST_IR_NEG:
        return real ? (narrow ? KEST_OP_NEG_F32 : KEST_OP_NEG_F)
                    : KEST_OP_NEG_I;
    case KEST_IR_AND:
        return KEST_OP_AND_I;
    case KEST_IR_OR:
        return KEST_OP_OR_I;
    case KEST_IR_XOR:
        return KEST_OP_XOR_I;
    case KEST_IR_FLIP:
        return KEST_OP_NOT_I;
    case KEST_IR_SHL:
        return KEST_OP_SHL;
    case KEST_IR_SHR:
        return without_sign ? KEST_OP_SHR_U : KEST_OP_SHR_I;
    default:
        return 0;
    }
}

// What a value of this type is written as. A number knows its own width and
// whether it has a sign, so the operation says "the text of this" and the type
// says which.
static uint8_t writes_text(const KestType *type) {
    if (type == NULL) {
        return KEST_OP_TEXT_I;
    }
    if (type->tag == KEST_T_FLAGS) {
        return KEST_OP_TEXT_FLAGS;
    }
    if (type->tag == KEST_T_ENUM || type->tag == KEST_T_OPTIONAL ||
        type->tag == KEST_T_STRUCT || type->tag == KEST_T_FIXED) {
        return KEST_OP_TEXT_VALUE;
    }
    if (type->tag == KEST_T_FLOAT) {
        return kest_is_narrow(type) ? KEST_OP_TEXT_F32 : KEST_OP_TEXT_F;
    }
    if (type->tag == KEST_T_BOOL) {
        return KEST_OP_TEXT_B;
    }
    return kest_is_unsigned(type) ? KEST_OP_TEXT_U : KEST_OP_TEXT_I;
}

// A branch whose landing place has not been written yet. Where the two bytes
// are is kept beside which operation they lead to, and both are filled in when
// the body is finished.
static void waits_for(Lower *lower, uint32_t target, KestSpan origin) {
    emit_u16(lower, 0, origin);
    if (lower->out_of_memory) {
        return;
    }
    lower->waiting[lower->wait_count] = lower->chunk->code_count - 2;
    lower->leads_to[lower->wait_count] = target;
    lower->wait_count++;
}

// A branch back to something already written, which is a distance rather than
// a place to fill in.
static void reaches_back(Lower *lower, uint32_t target, uint32_t after,
                         KestSpan origin) {
    uint32_t distance = lower->chunk->code_count + after - lower->at[target];
    if (distance > MAX_REACH) {
        refuse(lower, origin, "K0503",
               "this loop is %u bytes of code, and a loop reaches back %u",
               distance, (uint32_t)MAX_REACH);
        distance = 0;
    }
    emit_u16(lower, (uint16_t)distance, origin);
}

static void fill_in_branches(Lower *lower) {
    if (lower->out_of_memory) {
        return;
    }
    for (uint32_t i = 0; i < lower->wait_count; i++) {
        uint32_t placeholder = lower->waiting[i];
        uint32_t lands = lower->at[lower->leads_to[i]];
        uint32_t distance = lands - placeholder - 2;
        if (distance > MAX_REACH) {
            refuse(lower, lower->body->declared, "K0503",
                   "this jumps %u bytes of code, and a jump reaches %u",
                   distance, (uint32_t)MAX_REACH);
            continue;
        }
        lower->chunk->code[placeholder] = (uint8_t)(distance & 0xff);
        lower->chunk->code[placeholder + 1] = (uint8_t)(distance >> 8);
    }
}

// The branch a comparison or a `not` just before it is taken into. Twice at
// most: the jump takes back the `not` before it, and then the comparison that
// `not` was turning round. Both are one byte and both came through `emit`,
// which is what makes "the last instruction" a thing that can be known rather
// than guessed at from the bytes.
static uint8_t asks(Lower *lower, bool when_true) {
    uint8_t op = when_true ? KEST_OP_JUMP_TRUE : KEST_OP_JUMP_FALSE;
    for (uint32_t round = 0; fusing() && round < 2; round++) {
        // Only while the branch is still a plain one: one that has already
        // taken a comparison into itself is not looking for another.
        if ((op != KEST_OP_JUMP_FALSE && op != KEST_OP_JUMP_TRUE) ||
            lower->last_at + 1 != lower->chunk->code_count ||
            lower->last_at < lower->pointed_at) {
            break;
        }
        bool asking_true = op == KEST_OP_JUMP_TRUE;
        uint8_t fused =
            lower->last_op == KEST_OP_NOT
                ? (asking_true ? KEST_OP_JUMP_FALSE : KEST_OP_JUMP_TRUE)
                : fused_with_jump(lower->last_op, asking_true);
        if (fused == op) {
            break;
        }
        take_back(lower);
        op = fused;
    }
    return op;
}

// Reading a place into what is on the stack, and writing what is on the stack
// into one.
static void read_place(Lower *lower, const KestIrOp *op) {
    const KestIrPlace *place = &lower->body->places[op->place];
    switch (place->kind) {
    case KEST_IR_PLACE_SLOT:
        emit_load(lower, place->slot, place->slots, op->span);
        return;
    case KEST_IR_PLACE_RUN:
        emit(lower, KEST_OP_LOAD_SLOTS, op->span);
        emit_u16(lower, place->slot, op->span);
        emit_u16(lower, place->stride, op->span);
        emit_u16(lower, place->count, op->span);
        return;
    case KEST_IR_PLACE_ELEM:
        // Whether the handle and the index are read or left where they are is
        // what says which of the two this is: a read that consumes them is an
        // index, and one that does not is a place a write is coming to.
        if (op->arg_count == 0) {
            emit(lower, KEST_OP_LOAD_ELEM, op->span);
            emit_u16(lower, place->offset, op->span);
            emit_u16(lower, place->layout, op->span);
            return;
        }
        emit(lower, KEST_OP_INDEX, op->span);
        emit_u16(lower, place->layout, op->span);
        return;
    case KEST_IR_PLACE_AT:
        if (fusing() && elem_addr_before(lower)) {
            take_back(lower);
            emit(lower, KEST_OP_ELEM_AT, op->span);
            emit_u16(lower, place->offset, op->span);
            emit_u16(lower, place->layout, op->span);
            return;
        }
        emit(lower, KEST_OP_LOAD_AT, op->span);
        emit_u16(lower, place->offset, op->span);
        emit_u16(lower, place->layout, op->span);
        return;
    case KEST_IR_PLACE_HELD:
        break;
    }
    fault(lower, op->span, "this reads a kind of place with no instruction");
}

static void write_place(Lower *lower, const KestIrOp *op) {
    const KestIrPlace *place = &lower->body->places[op->place];
    switch (place->kind) {
    case KEST_IR_PLACE_SLOT:
        emit_store(lower, place->slot, place->slots, op->span);
        return;
    case KEST_IR_PLACE_RUN:
        emit(lower, KEST_OP_STORE_SLOTS, op->span);
        emit_u16(lower, place->slot, op->span);
        emit_u16(lower, place->stride, op->span);
        emit_u16(lower, place->count, op->span);
        return;
    case KEST_IR_PLACE_ELEM: {
        // And the same the other way round: a run of slots pushed and then
        // packed into the element. The push and the pack are the same slots.
        uint16_t from = 0;
        if (fusing() && place->layout < lower->module->layout_count &&
            load_before(lower, &from) ==
                lower->module->layouts[place->layout].slots &&
            lower->module->layouts[place->layout].slots > 1) {
            take_back(lower);
            emit(lower, KEST_OP_ELEM_FROM, op->span);
            emit_u16(lower, place->offset, op->span);
            emit_u16(lower, place->layout, op->span);
            emit_u16(lower, from, op->span);
            uint16_t wide = lower->module->layouts[place->layout].slots;
            if (wide > lower->chunk->fused_slots) {
                lower->chunk->fused_slots = wide;
            }
            return;
        }
        emit(lower, KEST_OP_STORE_ELEM, op->span);
        emit_u16(lower, place->offset, op->span);
        emit_u16(lower, place->layout, op->span);
        return;
    }
    case KEST_IR_PLACE_AT:
    case KEST_IR_PLACE_HELD:
        break;
    }
    fault(lower, op->span, "this writes a kind of place with no instruction");
}

static void take_address(Lower *lower, const KestIrOp *op) {
    const KestIrPlace *place = &lower->body->places[op->place];
    if (place->kind == KEST_IR_PLACE_ELEM) {
        emit(lower, KEST_OP_ELEM_ADDR, op->span);
        emit_u16(lower, place->layout, op->span);
        return;
    }
    if (place->kind == KEST_IR_PLACE_AT) {
        emit(lower, KEST_OP_OFFSET_ADDR, op->span);
        emit_u16(lower, place->stride, op->span);
        emit_u16(lower, place->count, op->span);
        return;
    }
    fault(lower, op->span, "this takes the address of a place that has none");
}

// How wide the value an operation leaves is, which is what says whether a
// `drop` is one instruction or two.
static uint16_t leaves(const Lower *lower, KestIrRef ref) {
    return ref < lower->body->value_count ? lower->body->values[ref].slots : 0;
}

static void lower_op(Lower *lower, uint32_t index, const KestIrOp *op) {
    KestSpan span = op->span;
    (void)index;
    switch ((KestIrKind)op->kind) {
    case KEST_IR_CONST:
        emit_constant(lower, op->imm[0], op->imm[1], span);
        return;
    case KEST_IR_CONST_AT: {
        uint32_t first = kest_chunk_constant_run(
            lower->module, lower->chunk, lower->body->constants + op->imm[0],
            lower->body->constant_classes + op->imm[0], op->imm[2] * op->imm[1]);
        if (lower->module->out_of_room) {
            lower->out_of_memory = true;
            return;
        }
        emit(lower, KEST_OP_CONST_AT, span);
        emit_u16(lower, (uint16_t)first, span);
        emit_u16(lower, op->imm[1], span);
        emit_u16(lower, op->imm[2], span);
        return;
    }
    case KEST_IR_TRUE:
        emit(lower, KEST_OP_TRUE, span);
        return;
    case KEST_IR_FALSE:
        emit(lower, KEST_OP_FALSE, span);
        return;
    case KEST_IR_LOAD:
        read_place(lower, op);
        return;
    case KEST_IR_PUT:
        write_place(lower, op);
        return;
    case KEST_IR_ADDR:
        take_address(lower, op);
        return;
    // A value out of its parts is already its parts, laid out where they were
    // made, and two ways of arriving at one value are one value. Neither is
    // an instruction here.
    case KEST_IR_MAKE:
    case KEST_IR_MEET:
    case KEST_IR_NOTHING:
        return;
    case KEST_IR_PART:
        // The front of a run is the run with what is above it dropped, which
        // is cheaper than reaching into it and is the same answer.
        if (op->imm[0] == 0) {
            if (op->imm[2] > op->imm[1]) {
                emit(lower, KEST_OP_POPN, span);
                emit_u16(lower, (uint16_t)(op->imm[2] - op->imm[1]), span);
            }
            return;
        }
        emit(lower, KEST_OP_FIELD, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        emit_u16(lower, op->imm[2], span);
        return;
    case KEST_IR_TURN:
        emit(lower, KEST_OP_ROTATE, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_DROP: {
        uint16_t wide = leaves(lower, lower->body->args[op->first_arg]);
        if (wide == 1) {
            emit(lower, KEST_OP_POP, span);
            return;
        }
        emit(lower, KEST_OP_POPN, span);
        emit_u16(lower, wide, span);
        return;
    }

    case KEST_IR_ADD:
    case KEST_IR_SUB:
    case KEST_IR_MUL:
    case KEST_IR_DIV:
    case KEST_IR_MOD:
    case KEST_IR_NEG:
    case KEST_IR_AND:
    case KEST_IR_OR:
    case KEST_IR_XOR:
    case KEST_IR_FLIP:
    case KEST_IR_SHL:
    case KEST_IR_SHR: {
        uint8_t does = arithmetic(op->kind, op->type);
        if (does == 0) {
            fault(lower, span, "this is arithmetic with no instruction");
            return;
        }
        emit(lower, does, span);
        return;
    }
    case KEST_IR_NARROW: {
        uint8_t does = KEST_OP_NARROW;
        if (fusing() && lower->last_at + 1 == lower->chunk->code_count &&
            lower->last_at >= lower->pointed_at) {
            does = fused_with_narrow(lower->last_op);
            if (does != KEST_OP_NARROW) {
                take_back(lower);
            }
        }
        emit(lower, does, span);
        emit_u16(lower, op->imm[0], span);
        return;
    }
    case KEST_IR_TO_FLOAT:
        emit(lower, kest_is_unsigned(op->type) ? KEST_OP_U2F : KEST_OP_I2F,
             span);
        return;
    case KEST_IR_TO_WHOLE:
        emit(lower, KEST_OP_F2I, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_TO_F32:
        emit(lower, KEST_OP_TO_F32, span);
        return;

    case KEST_IR_LT:
    case KEST_IR_LE:
    case KEST_IR_GT:
    case KEST_IR_GE:
    case KEST_IR_EQ:
    case KEST_IR_NE:
        if (is_a_run(op->type)) {
            emit(lower, op->kind == KEST_IR_EQ ? KEST_OP_EQ_VALUE
                                               : KEST_OP_NE_VALUE,
                 span);
            emit_u16(lower, op->imm[0], span);
            return;
        }
        {
            uint8_t does = compares(op->kind, op->type);
            if (does == 0) {
                fault(lower, span, "this compares with no instruction");
                return;
            }
            emit(lower, does, span);
        }
        return;
    case KEST_IR_NOT:
        emit(lower, KEST_OP_NOT, span);
        return;
    case KEST_IR_HASH:
        // A reference is one slot and goes through the walk that knows what a
        // value is made of, because only part of it may be hashed: the place
        // is the program's and the number above it is the process's. See
        // D1054.
        if (is_a_run(op->type) ||
            (op->type != NULL && op->type->tag == KEST_T_REF)) {
            emit(lower, KEST_OP_HASH_VALUE, span);
            emit_u16(lower, op->imm[0], span);
            return;
        }
        emit(lower,
             op->type != NULL && op->type->tag == KEST_T_TEXT ? KEST_OP_HASH_T
             : is_float(op->type)                             ? KEST_OP_HASH_F
                                                              : KEST_OP_HASH_I,
             span);
        return;

    case KEST_IR_TEXT_LEN:
        emit(lower, KEST_OP_TEXT_LEN, span);
        return;
    case KEST_IR_TEXT_AT:
        emit(lower, KEST_OP_TEXT_AT, span);
        return;
    case KEST_IR_TEXT_IN:
        emit(lower, KEST_OP_TEXT_IN, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        return;
    case KEST_IR_TEXT_SLICE:
        emit(lower, KEST_OP_TEXT_SLICE, span);
        return;
    case KEST_IR_TEXT_REST:
        emit(lower, KEST_OP_TEXT_REST, span);
        return;
    case KEST_IR_TEXT_MATCHES:
        emit(lower, KEST_OP_TEXT_MATCHES, span);
        return;
    case KEST_IR_TEXT_FIND:
        emit(lower, KEST_OP_TEXT_FIND, span);
        return;
    case KEST_IR_TEXT_OF: {
        uint8_t does = writes_text(op->type);
        emit(lower, does, span);
        if (does == KEST_OP_TEXT_FLAGS || does == KEST_OP_TEXT_VALUE) {
            emit_u16(lower, op->imm[0], span);
        }
        return;
    }
    case KEST_IR_TEXT_JOIN:
        emit(lower, KEST_OP_CONCAT, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_TEXT_FROM:
        emit(lower, KEST_OP_TEXT_FROM, span);
        return;

    case KEST_IR_ARRAY:
        emit(lower, KEST_OP_ARRAY, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        return;
    case KEST_IR_ARRAY_NEW:
        emit(lower, KEST_OP_MAKE_ARRAY, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_LEN:
        emit(lower, KEST_OP_LEN, span);
        return;
    case KEST_IR_APPEND:
        emit(lower, KEST_OP_PUSH, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_FIT:
        emit(lower, KEST_OP_FIT, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_ROOM:
        emit(lower, KEST_OP_ROOM, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_POP_LAST:
        emit(lower, KEST_OP_POP_LAST, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_TAKE:
        emit(lower, KEST_OP_TAKE, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_CLEAR:
        emit(lower, KEST_OP_CLEAR, span);
        return;

    case KEST_IR_STORE_NEW:
        emit(lower, KEST_OP_NEW_STORE, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        return;
    case KEST_IR_STORE_ADD:
        emit(lower, KEST_OP_ADD, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_STORE_GET:
        emit(lower, KEST_OP_GET, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_STORE_SET:
        emit(lower, KEST_OP_SET, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_STORE_REMOVE:
        emit(lower, KEST_OP_REMOVE, span);
        return;
    case KEST_IR_STORE_COUNT:
        emit(lower, KEST_OP_COUNT, span);
        return;
    case KEST_IR_STORE_REF:
        emit(lower, KEST_OP_STORE_REF, span);
        return;
    case KEST_IR_NEXT:
        emit(lower, kest_is_unsigned(op->type) ? KEST_OP_NEXT_LESS_U
                                               : KEST_OP_NEXT_LESS_I,
             span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        reaches_back(lower, op->target, 2, span);
        return;
    case KEST_IR_SEEK_FROM:
        emit(lower, KEST_OP_SEEK_FROM, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        waits_for(lower, op->target, span);
        return;
    case KEST_IR_SEEK_NEXT:
        emit(lower, KEST_OP_SEEK_NEXT, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        reaches_back(lower, op->target, 2, span);
        return;

    case KEST_IR_CALL:
        emit(lower, KEST_OP_CALL, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        return;
    case KEST_IR_CALL_VALUE:
        emit(lower, KEST_OP_CALL_VALUE, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        return;
    case KEST_IR_CALL_HOST:
        emit(lower, KEST_OP_CALL_HOST, span);
        emit_u16(lower, op->imm[0], span);
        emit_u16(lower, op->imm[1], span);
        emit_u16(lower, op->imm[2], span);
        return;

    case KEST_IR_REGION_OPEN:
        emit(lower, KEST_OP_SCRATCH, span);
        emit_u16(lower, op->imm[0], span);
        return;
    case KEST_IR_REGION_CLOSE:
        emit(lower, KEST_OP_UNSCRATCH, span);
        emit_u16(lower, op->imm[0], span);
        return;

    case KEST_IR_GO:
        // Back to where a loop began, or on to somewhere not written yet.
        // Which of the two it is is which way it goes.
        if (op->target <= index) {
            emit(lower, KEST_OP_LOOP, span);
            reaches_back(lower, op->target, 2, span);
            return;
        }
        emit(lower, KEST_OP_JUMP, span);
        waits_for(lower, op->target, span);
        return;
    case KEST_IR_ASK:
        emit(lower, asks(lower, op->imm[1] != 0), span);
        waits_for(lower, op->target, span);
        return;
    case KEST_IR_GIVE:
        emit(lower, KEST_OP_RETURN, span);
        emit_u16(lower, op->imm[0], span);
        return;

    case KEST_IR_OP_COUNT:
        break;
    }
    kest_diags_disagree(lower->program->diags, span,
                        "`%s` is an operation with no instruction",
                        kest_ir_word((KestIrKind)op->kind));
}

// How wide each argument is, kept beside how wide they are together: a host
// filling a frame asks where the second one starts rather than working it out
// from the first one's fields.
static bool remember_takes(Lower *lower, const KestType *signature) {
    if (signature == NULL) {
        return true;
    }
    if (signature->result != NULL && signature->result->tag != KEST_T_VOID) {
        int32_t gives = kest_module_layout(lower->module, signature->result);
        if (gives < 0) {
            return false;
        }
        lower->chunk->gives = (uint16_t)gives;
    }
    if (signature->param_count == 0) {
        return true;
    }
    uint16_t *widths = KEST_ARENA_ARRAY(lower->module->arena, uint16_t,
                                        signature->param_count);
    if (widths == NULL) {
        return false;
    }
    for (uint32_t p = 0; p < signature->param_count; p++) {
        int32_t wide = kest_module_layout(lower->module, signature->params[p]);
        if (wide < 0) {
            return false;
        }
        widths[p] = (uint16_t)wide;
    }
    lower->chunk->takes = widths;
    lower->chunk->takes_count = (uint16_t)signature->param_count;
    return true;
}

static bool lower_body(Lower *lower, const KestIrBody *body, KestChunk *chunk) {
    lower->body = body;
    lower->chunk = chunk;
    lower->wait_count = 0;
    lower->last_op = 0;
    lower->last_at = 0;
    lower->before_op = 0;
    lower->before_at = 0;
    lower->pointed_at = 0;

    // In the bodies' own arena: what is worked out here is read while this one
    // body is written and by nothing after it.
    KestArena *arena = lower->arena;
    uint32_t count = body->op_count == 0 ? 1 : body->op_count;
    lower->at = KEST_ARENA_ARRAY(arena, uint32_t, count);
    lower->landed_on = KEST_ARENA_ARRAY(arena, bool, count);
    lower->waiting = KEST_ARENA_ARRAY(arena, uint32_t, count);
    lower->leads_to = KEST_ARENA_ARRAY(arena, uint32_t, count);
    if (lower->at == NULL || lower->landed_on == NULL ||
        lower->waiting == NULL || lower->leads_to == NULL) {
        lower->out_of_memory = true;
        return false;
    }

    // Where anything branches to, read before anything is written: two
    // instructions are only made into one where nothing lands between them.
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if ((op->kind == KEST_IR_GO || op->kind == KEST_IR_ASK ||
             op->kind == KEST_IR_NEXT || op->kind == KEST_IR_SEEK_FROM ||
             op->kind == KEST_IR_SEEK_NEXT) &&
            op->target < body->op_count) {
            lower->landed_on[op->target] = true;
        }
    }

    chunk->param_slots = body->param_slots;
    if (!remember_takes(lower, body->signature)) {
        lower->out_of_memory = true;
        return false;
    }

    for (uint32_t i = 0; i < body->op_count; i++) {
        lower->at[i] = chunk->code_count;
        if (lower->landed_on[i]) {
            lower->pointed_at = chunk->code_count;
        }
        lower_op(lower, i, &body->ops[i]);
        if (lower->out_of_memory) {
            return false;
        }
    }
    fill_in_branches(lower);

    chunk->slot_count = body->slot_count;
    chunk->stack_needed = body->stack_needed;
    chunk->folded = body->folded;
    chunk->folded_slots = body->folded_slots;
    return !lower->out_of_memory;
}

KestLower *kest_lower_new(KestProgram *program, KestModule *module,
                          KestArena *arena) {
    KestLower *lower = KEST_ARENA_NEW(arena, KestLower);
    if (lower == NULL) {
        return NULL;
    }
    lower->program = program;
    lower->module = module;
    lower->arena = arena;
    return lower;
}

bool kest_lower_body(void *reading, const KestIrBody *body) {
    Lower *lower = reading;
    if (lower->next >= lower->module->count) {
        return false;
    }
    kest_diags_in(lower->program->diags, body->source);
    // A body that is not what a body is would be written as instructions that
    // mean something else, so it is refused rather than written.
    const char *wrong = kest_ir_verify(body);
    if (wrong != NULL) {
        kest_diags_disagree(lower->program->diags, body->declared, "%s", wrong);
        return false;
    }
    KestChunk *into = lower->module->functions[lower->next++];
    // What the optimizer took out before this read the body, which the
    // compiler's reckoning of how deep the stack goes still counts. It sits
    // beside what this file's own fusions save and is held the same way. See
    // D1012 and D1025.
    if (body->took_slots > into->fused_slots) {
        into->fused_slots = body->took_slots;
    }
    // What the body called its slots, carried through so a stopped machine can
    // say `hungry` rather than `slot 4`. The IR has kept them since D962
    // because the escape pass wanted them; this is the second reader. See
    // D991.
    for (uint32_t i = 0; i < body->name_count; i++) {
        const KestIrName *one = &body->names[i];
        kest_chunk_names(lower->module, into, one->name, one->slot,
                         one->slots,
                         one->type == NULL ? (uint8_t)KEST_L_WORD
                                           : kest_scalar_of(one->type));
    }
    return lower_body(lower, body, into);
}
