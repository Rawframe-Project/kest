#include "slots.h"

#include <string.h>

#define UNTOLD 0xFFFFFFFFu

// What a value is read as. A stack machine has one answer — the top — and this
// one has three: where the body put it, a place the body already had, or a
// value the chunk holds and nothing has to move at all.
typedef struct {
    uint16_t as;
    // Whether anything has to be written for the operation that made it.
    bool made_here;
    // How many read it, because a value read twice cannot be a place that
    // might be written between the two readings.
    uint16_t readers;
} Held;

typedef struct {
    KestSlotWriter *writer;
    const KestIrBody *body;
    KestSlotBody *out;
    const uint16_t *window;
    Held *held;
    uint32_t *at;
    uint32_t *waiting;
    uint32_t *leads_to;
    uint32_t wait_count;
    bool refused;
    const char *why;
} Writing;

static void cannot(Writing *writing, const char *why) {
    if (!writing->refused) {
        writing->refused = true;
        writing->why = why;
    }
}

static void put(Writing *writing, uint8_t byte) {
    KestSlotBody *out = writing->out;
    if (out->code_count == out->code_capacity) {
        uint32_t grown = out->code_capacity == 0 ? 64 : out->code_capacity * 2;
        uint8_t *moved = KEST_ARENA_ARRAY(writing->writer->arena, uint8_t,
                                          grown);
        if (moved == NULL) {
            writing->writer->out_of_memory = true;
            cannot(writing, "there was no room");
            return;
        }
        if (out->code_count > 0) {
            memcpy(moved, out->code, out->code_count);
        }
        out->code = moved;
        out->code_capacity = grown;
    }
    out->code[out->code_count++] = byte;
}

// An operand, which is one byte. A body that wants more places than that is
// refused rather than made to carry two bytes everywhere.
static void put_u16(Writing *writing, uint16_t value) {
    if (value >= KEST_SLOT_MOST && (value & KEST_SLOT_HELD) == 0) {
        cannot(writing, "a body with more places in it than this machine names");
        return;
    }
    put(writing, (uint8_t)(value & 0xff));
}

static void put_where(Writing *writing, uint32_t value) {
    put(writing, (uint8_t)(value & 0xff));
    put(writing, (uint8_t)(value >> 8));
}

// Where a value is read from. Everything below this decides that once, before
// anything is written, so that reading it here is a lookup.
static uint16_t reads(Writing *writing, uint32_t which) {
    KestIrRef ref = writing->body->args[which];
    return writing->held[ref].as;
}

static uint16_t reads_arg(Writing *writing, const KestIrOp *op, uint16_t a) {
    return reads(writing, op->first_arg + a);
}

static uint16_t writes_to(Writing *writing, const KestIrOp *op) {
    return writing->held[op->dest].as;
}

static bool is_float(const KestType *type) {
    return type != NULL && type->tag == KEST_T_FLOAT;
}

static bool is_a_run(const KestType *type) {
    return type != NULL &&
           (type->tag == KEST_T_ENUM || type->tag == KEST_T_STRUCT ||
            type->tag == KEST_T_FIXED);
}

// The instruction for an operation and what it is on, which is the same
// question the stack backend asks and the same answer: the type says which
// family, and there is one addition in a body.
static uint8_t works(uint16_t operation, const KestType *type) {
    bool real = is_float(type);
    bool narrow = kest_is_narrow(type);
    bool plain = !kest_is_unsigned(type);
    switch (operation) {
    case KEST_IR_ADD:
        return real ? (narrow ? KEST_SLOT_ADD_F32 : KEST_SLOT_ADD_F)
                    : KEST_SLOT_ADD_I;
    case KEST_IR_SUB:
        return real ? (narrow ? KEST_SLOT_SUB_F32 : KEST_SLOT_SUB_F)
                    : KEST_SLOT_SUB_I;
    case KEST_IR_MUL:
        return real ? (narrow ? KEST_SLOT_MUL_F32 : KEST_SLOT_MUL_F)
                    : KEST_SLOT_MUL_I;
    case KEST_IR_DIV:
        return real ? (narrow ? KEST_SLOT_DIV_F32 : KEST_SLOT_DIV_F)
                    : (plain ? KEST_SLOT_DIV_I : KEST_SLOT_DIV_U);
    case KEST_IR_MOD:
        return real ? KEST_SLOT_OP_COUNT
                    : (plain ? KEST_SLOT_MOD_I : KEST_SLOT_MOD_U);
    case KEST_IR_NEG:
        return real ? (narrow ? KEST_SLOT_NEG_F32 : KEST_SLOT_NEG_F)
                    : KEST_SLOT_NEG_I;
    case KEST_IR_AND:
        return KEST_SLOT_AND_I;
    case KEST_IR_OR:
        return KEST_SLOT_OR_I;
    case KEST_IR_XOR:
        return KEST_SLOT_XOR_I;
    case KEST_IR_SHL:
        return KEST_SLOT_SHL;
    case KEST_IR_SHR:
        return plain ? KEST_SLOT_SHR_I : KEST_SLOT_SHR_U;
    default:
        return KEST_SLOT_OP_COUNT;
    }
}

static uint8_t compares(uint16_t operation, const KestType *type) {
    bool real = is_float(type);
    bool plain = !kest_is_unsigned(type);
    switch (operation) {
    case KEST_IR_LT:
        return real ? KEST_SLOT_LT_F
                    : (plain ? KEST_SLOT_LT_I : KEST_SLOT_LT_U);
    case KEST_IR_LE:
        return real ? KEST_SLOT_LE_F
                    : (plain ? KEST_SLOT_LE_I : KEST_SLOT_LE_U);
    case KEST_IR_GT:
        return real ? KEST_SLOT_GT_F
                    : (plain ? KEST_SLOT_GT_I : KEST_SLOT_GT_U);
    case KEST_IR_GE:
        return real ? KEST_SLOT_GE_F
                    : (plain ? KEST_SLOT_GE_I : KEST_SLOT_GE_U);
    case KEST_IR_EQ:
        return real ? KEST_SLOT_EQ_F : KEST_SLOT_EQ_I;
    case KEST_IR_NE:
        return real ? KEST_SLOT_NE_F : KEST_SLOT_NE_I;
    default:
        return KEST_SLOT_OP_COUNT;
    }
}

// Whether an operation reads its arguments one slot at a time, which is what
// says a value it reads may be left where it already is rather than put
// somewhere this body chose. The three that cannot are the ones that read a run
// and need it where the run belongs: a value made of its parts, a call whose
// arguments are the frame it is about to write, and the meeting of two ways of
// arriving at one answer.
static bool reads_a_place(uint16_t kind) {
    switch (kind) {
    case KEST_IR_MAKE:
    case KEST_IR_CALL:
    case KEST_IR_MEET:
        return false;
    default:
        return true;
    }
}

// And whether it reads them as numbers rather than as slots, which is what says
// a value the chunk holds can be read where it is.
static bool reads_a_number(uint16_t kind) {
    switch (kind) {
    case KEST_IR_ADD:
    case KEST_IR_SUB:
    case KEST_IR_MUL:
    case KEST_IR_DIV:
    case KEST_IR_MOD:
    case KEST_IR_NEG:
    case KEST_IR_AND:
    case KEST_IR_OR:
    case KEST_IR_XOR:
    case KEST_IR_SHL:
    case KEST_IR_SHR:
    case KEST_IR_NARROW:
    case KEST_IR_NOT:
    case KEST_IR_LT:
    case KEST_IR_LE:
    case KEST_IR_GT:
    case KEST_IR_GE:
    case KEST_IR_EQ:
    case KEST_IR_NE:
    case KEST_IR_ASK:
    case KEST_IR_LEN:
    case KEST_IR_LOAD:
    case KEST_IR_PUT:
        return true;
    default:
        return false;
    }
}

// Which place each value is read from, decided before anything is written. A
// value made by reading a name can be read from the name, and one that is a
// number the chunk holds can be read from the chunk: between them they are most
// of what a stack machine spends its instructions pushing.
static void decide(Writing *writing) {
    const KestIrBody *body = writing->body;
    uint16_t names = body->slot_count;
    for (uint32_t v = 0; v < body->value_count; v++) {
        writing->held[v].as = (uint16_t)(names + writing->window[v]);
        writing->held[v].made_here = true;
        writing->held[v].readers = 0;
    }
    uint32_t *only = KEST_ARENA_ARRAY(writing->writer->arena, uint32_t,
                                      body->value_count == 0 ? 1
                                                             : body->value_count);
    bool *lands = KEST_ARENA_ARRAY(writing->writer->arena, bool,
                                   body->op_count == 0 ? 1 : body->op_count);
    if (only == NULL || lands == NULL) {
        writing->writer->out_of_memory = true;
        cannot(writing, "there was no room");
        return;
    }
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        for (uint16_t a = 0; a < op->arg_count; a++) {
            KestIrRef ref = body->args[op->first_arg + a];
            only[ref] = i;
            writing->held[ref].readers++;
        }
        if (op->target < body->op_count &&
            (op->kind == KEST_IR_GO || op->kind == KEST_IR_ASK ||
             op->kind == KEST_IR_NEXT || op->kind == KEST_IR_SEEK_FROM ||
             op->kind == KEST_IR_SEEK_NEXT)) {
            lands[op->target] = true;
        }
    }
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if (op->dest == KEST_IR_NONE || writing->held[op->dest].readers != 1) {
            continue;
        }
        const KestIrOp *reader = &body->ops[only[op->dest]];
        if (op->kind == KEST_IR_CONST && op->imm[1] == 1 &&
            reads_a_number(reader->kind)) {
            uint32_t index = kest_chunk_constant_run(
                writing->writer->module,
                writing->writer->module->functions[writing->writer->next],
                body->constants + op->imm[0],
                body->constant_classes + op->imm[0], 1);
            if (index < KEST_SLOT_MOST) {
                writing->held[op->dest].as = (uint16_t)(KEST_SLOT_HELD | index);
                writing->held[op->dest].made_here = false;
            }
            continue;
        }
        if (op->kind != KEST_IR_LOAD || op->arg_count != 0 ||
            op->place == KEST_IR_NO_PLACE ||
            body->places[op->place].kind != KEST_IR_PLACE_SLOT ||
            !reads_a_place(reader->kind)) {
            continue;
        }
        const KestIrPlace *place = &body->places[op->place];
        bool safe = true;
        for (uint32_t between = i + 1; between < only[op->dest] && safe;
             between++) {
            const KestIrOp *other = &body->ops[between];
            if (lands[between] || other->kind == KEST_IR_GO ||
                other->kind == KEST_IR_ASK || other->kind == KEST_IR_NEXT) {
                safe = false;
                break;
            }
            if (other->kind != KEST_IR_PUT ||
                other->place == KEST_IR_NO_PLACE) {
                continue;
            }
            const KestIrPlace *wrote = &body->places[other->place];
            if (wrote->kind == KEST_IR_PLACE_SLOT &&
                wrote->slot + wrote->slots > place->slot &&
                place->slot + place->slots > wrote->slot) {
                safe = false;
            }
            if (wrote->kind == KEST_IR_PLACE_RUN) {
                uint32_t wide = (uint32_t)wrote->stride * wrote->count;
                if (wrote->slot + wide > place->slot &&
                    place->slot + place->slots > wrote->slot) {
                    safe = false;
                }
            }
        }
        if (safe) {
            writing->held[op->dest].as = place->slot;
            writing->held[op->dest].made_here = false;
        }
    }
}

static void goes_to(Writing *writing, uint32_t target) {
    writing->waiting[writing->wait_count] = writing->out->code_count;
    writing->leads_to[writing->wait_count] = target;
    writing->wait_count++;
    put_where(writing, 0);
}

static void write_op(Writing *writing, const KestIrOp *op) {
    const KestIrBody *body = writing->body;
    switch ((KestIrKind)op->kind) {
    case KEST_IR_CONST:
        if (!writing->held[op->dest].made_here) {
            return;
        }
        {
            uint32_t index = kest_chunk_constant_run(
                writing->writer->module,
                writing->writer->module->functions[writing->writer->next],
                body->constants + op->imm[0],
                body->constant_classes + op->imm[0], op->imm[1]);
            if (index >= KEST_SLOT_MOST) {
                cannot(writing, "a body with more values in it than this "
                                "machine can name");
                return;
            }
            put(writing, KEST_SLOT_MOVE);
            put_u16(writing, writes_to(writing, op));
            put_u16(writing, (uint16_t)(KEST_SLOT_HELD | index));
            put_u16(writing, op->imm[1]);
        }
        return;
    case KEST_IR_TRUE:
    case KEST_IR_FALSE:
        put(writing, op->kind == KEST_IR_TRUE ? KEST_SLOT_TRUE
                                              : KEST_SLOT_FALSE);
        put_u16(writing, writes_to(writing, op));
        return;
    case KEST_IR_LOAD: {
        const KestIrPlace *place = &body->places[op->place];
        if (place->kind == KEST_IR_PLACE_SLOT && op->arg_count == 0) {
            if (!writing->held[op->dest].made_here) {
                return;
            }
            put(writing, KEST_SLOT_MOVE);
            put_u16(writing, writes_to(writing, op));
            put_u16(writing, place->slot);
            put_u16(writing, place->slots);
            return;
        }
        if (place->kind == KEST_IR_PLACE_ELEM && op->arg_count == 2) {
            put(writing, KEST_SLOT_ELEM);
            put_u16(writing, writes_to(writing, op));
            put_u16(writing, reads_arg(writing, op, 0));
            put_u16(writing, reads_arg(writing, op, 1));
            put_u16(writing, place->layout);
            return;
        }
        cannot(writing, "a read this machine has no instruction for");
        return;
    }
    case KEST_IR_PUT: {
        const KestIrPlace *place = &body->places[op->place];
        if (place->kind == KEST_IR_PLACE_SLOT && op->arg_count == 1) {
            put(writing, KEST_SLOT_MOVE);
            put_u16(writing, place->slot);
            put_u16(writing, reads_arg(writing, op, 0));
            put_u16(writing, place->slots);
            return;
        }
        if (place->kind == KEST_IR_PLACE_ELEM && op->arg_count == 3) {
            put(writing, KEST_SLOT_SET_ELEM);
            put_u16(writing, reads_arg(writing, op, 0));
            put_u16(writing, reads_arg(writing, op, 1));
            put_u16(writing, reads_arg(writing, op, 2));
            put_u16(writing, place->layout);
            return;
        }
        cannot(writing, "a write this machine has no instruction for");
        return;
    }
    // A value out of its parts is its parts, each already where it belongs;
    // two ways to one answer are one answer in one place; and dropping a value
    // this machine never pushed is nothing at all.
    case KEST_IR_MAKE:
    case KEST_IR_MEET:
    case KEST_IR_DROP:
        return;
    case KEST_IR_PART:
        put(writing, KEST_SLOT_PART);
        put_u16(writing, writes_to(writing, op));
        put_u16(writing, reads_arg(writing, op, 0));
        put_u16(writing, op->imm[0]);
        put_u16(writing, op->imm[1]);
        return;
    case KEST_IR_LEN:
        put(writing, KEST_SLOT_LEN);
        put_u16(writing, writes_to(writing, op));
        put_u16(writing, reads_arg(writing, op, 0));
        return;
    case KEST_IR_NOT:
        put(writing, KEST_SLOT_NOT);
        put_u16(writing, writes_to(writing, op));
        put_u16(writing, reads_arg(writing, op, 0));
        return;
    case KEST_IR_NARROW:
        put(writing, KEST_SLOT_NARROW);
        put_u16(writing, writes_to(writing, op));
        put_u16(writing, reads_arg(writing, op, 0));
        put_u16(writing, op->imm[0]);
        return;
    case KEST_IR_ADD:
    case KEST_IR_SUB:
    case KEST_IR_MUL:
    case KEST_IR_DIV:
    case KEST_IR_MOD:
    case KEST_IR_AND:
    case KEST_IR_OR:
    case KEST_IR_XOR:
    case KEST_IR_SHL:
    case KEST_IR_SHR:
    case KEST_IR_NEG: {
        uint8_t does = works(op->kind, op->type);
        if (does == KEST_SLOT_OP_COUNT) {
            cannot(writing, "arithmetic this machine has no instruction for");
            return;
        }
        put(writing, does);
        put_u16(writing, writes_to(writing, op));
        put_u16(writing, reads_arg(writing, op, 0));
        if (op->kind != KEST_IR_NEG) {
            put_u16(writing, reads_arg(writing, op, 1));
        }
        return;
    }
    case KEST_IR_LT:
    case KEST_IR_LE:
    case KEST_IR_GT:
    case KEST_IR_GE:
    case KEST_IR_EQ:
    case KEST_IR_NE: {
        if (is_a_run(op->type) ||
            (op->type != NULL && op->type->tag == KEST_T_TEXT)) {
            cannot(writing, "a comparison of a run this machine has no "
                            "instruction for");
            return;
        }
        uint8_t does = compares(op->kind, op->type);
        put(writing, does);
        put_u16(writing, writes_to(writing, op));
        put_u16(writing, reads_arg(writing, op, 0));
        put_u16(writing, reads_arg(writing, op, 1));
        return;
    }
    case KEST_IR_CALL: {
        uint16_t first = op->arg_count > 0 ? reads_arg(writing, op, 0)
                                           : writes_to(writing, op);
        KestSlotBody *out = writing->out;
        if (out->call_count == out->call_capacity) {
            uint32_t grown = out->call_capacity == 0 ? 4
                                                     : out->call_capacity * 2;
            uint16_t *moved =
                KEST_ARENA_ARRAY(writing->writer->arena, uint16_t, grown);
            if (moved == NULL) {
                writing->writer->out_of_memory = true;
                cannot(writing, "there was no room");
                return;
            }
            if (out->call_count > 0) {
                memcpy(moved, out->calls, sizeof(uint16_t) * out->call_count);
            }
            out->calls = moved;
            out->call_capacity = grown;
        }
        out->calls[out->call_count++] = op->imm[0];
        put(writing, KEST_SLOT_CALL);
        put_u16(writing, op->dest == KEST_IR_NONE ? first
                                                  : writes_to(writing, op));
        put_u16(writing, op->imm[0]);
        put_u16(writing, first);
        put_u16(writing, op->imm[1]);
        return;
    }
    case KEST_IR_GO:
        put(writing, KEST_SLOT_GO);
        goes_to(writing, op->target);
        return;
    case KEST_IR_ASK:
        put(writing, op->imm[1] != 0 ? KEST_SLOT_GO_TRUE : KEST_SLOT_GO_FALSE);
        put_u16(writing, reads_arg(writing, op, 0));
        goes_to(writing, op->target);
        return;
    case KEST_IR_NEXT:
        put(writing, kest_is_unsigned(op->type) ? KEST_SLOT_NEXT_LESS_U
                                                : KEST_SLOT_NEXT_LESS_I);
        put_u16(writing, op->imm[0]);
        put_u16(writing, op->imm[1]);
        goes_to(writing, op->target);
        return;
    case KEST_IR_GIVE:
        put(writing, KEST_SLOT_GIVE);
        put_u16(writing, op->arg_count > 0 ? reads_arg(writing, op, 0) : 0);
        put_u16(writing, op->imm[0]);
        return;
    default:
        cannot(writing, kest_ir_word((KestIrKind)op->kind));
        return;
    }
}

KestSlotWriter *kest_slots_new(KestProgram *program, KestModule *module,
                               KestArena *arena) {
    KestSlotWriter *writer = KEST_ARENA_NEW(arena, KestSlotWriter);
    if (writer == NULL) {
        return NULL;
    }
    writer->arena = arena;
    writer->module = module;
    writer->program = program;
    return writer;
}

bool kest_slots_body(void *writing_to, const KestIrBody *body) {
    KestSlotWriter *writer = writing_to;
    if (writer->count == writer->capacity) {
        uint32_t grown = writer->capacity == 0 ? 16 : writer->capacity * 2;
        KestSlotBody *moved =
            KEST_ARENA_ARRAY(writer->arena, KestSlotBody, grown);
        if (moved == NULL) {
            writer->out_of_memory = true;
            return false;
        }
        if (writer->count > 0) {
            memcpy(moved, writer->bodies, sizeof(KestSlotBody) * writer->count);
        }
        writer->bodies = moved;
        writer->capacity = grown;
    }
    KestSlotBody *out = &writer->bodies[writer->count++];
    memset(out, 0, sizeof *out);
    writer->next = writer->count - 1;

    uint16_t *window = NULL;
    uint16_t deepest = 0;
    const char *wrong = kest_ir_windows(body, writer->arena, &window, &deepest);
    if (wrong != NULL) {
        writer->refused++;
        if (writer->why == NULL) {
            writer->why = wrong;
        }
        return true;
    }
    Writing writing = {0};
    writing.writer = writer;
    writing.body = body;
    writing.out = out;
    writing.window = window;
    writing.held = KEST_ARENA_ARRAY(writer->arena, Held,
                                    body->value_count == 0 ? 1
                                                           : body->value_count);
    writing.at = KEST_ARENA_ARRAY(writer->arena, uint32_t,
                                  body->op_count == 0 ? 1 : body->op_count);
    writing.waiting = KEST_ARENA_ARRAY(writer->arena, uint32_t,
                                       body->op_count == 0 ? 1
                                                           : body->op_count);
    writing.leads_to = KEST_ARENA_ARRAY(writer->arena, uint32_t,
                                        body->op_count == 0 ? 1
                                                            : body->op_count);
    if (writing.held == NULL || writing.at == NULL || writing.waiting == NULL ||
        writing.leads_to == NULL) {
        writer->out_of_memory = true;
        return false;
    }
    out->frame_slots = (uint16_t)(body->slot_count + deepest);
    if (out->frame_slots >= KEST_SLOT_MOST) {
        writer->refused++;
        if (writer->why == NULL) {
            writer->why = "a body with more places in it than this machine "
                          "names";
        }
        return true;
    }
    decide(&writing);
    for (uint32_t i = 0; i < body->op_count && !writing.refused; i++) {
        writing.at[i] = out->code_count;
        uint32_t was = out->code_count;
        write_op(&writing, &body->ops[i]);
        if (out->code_count != was) {
            out->instructions++;
        }
    }
    if (writing.refused) {
        writer->refused++;
        if (writer->why == NULL) {
            writer->why = writing.why;
        }
        out->code_count = 0;
        return !writer->out_of_memory;
    }
    for (uint32_t i = 0; i < writing.wait_count; i++) {
        uint32_t lands = writing.leads_to[i] < body->op_count
                             ? writing.at[writing.leads_to[i]]
                             : out->code_count;
        out->code[writing.waiting[i]] = (uint8_t)(lands & 0xff);
        out->code[writing.waiting[i] + 1] = (uint8_t)(lands >> 8);
    }
    out->written = true;
    return true;
}

static bool reaches(const KestSlotWriter *writer, uint32_t at, bool *seen) {
    if (at >= writer->count || seen[at]) {
        return at < writer->count;
    }
    seen[at] = true;
    if (!writer->bodies[at].written) {
        return false;
    }
    for (uint32_t i = 0; i < writer->bodies[at].call_count; i++) {
        if (!reaches(writer, writer->bodies[at].calls[i], seen)) {
            return false;
        }
    }
    return true;
}

bool kest_slots_reaches(const KestSlotWriter *writer, uint32_t entry) {
    if (writer == NULL || entry >= writer->count) {
        return false;
    }
    bool *seen = KEST_ARENA_ARRAY(writer->arena, bool, writer->count);
    if (seen == NULL) {
        return false;
    }
    return reaches(writer, entry, seen);
}

const KestSlotBody *kest_slots_of(const KestSlotWriter *writer, uint32_t at) {
    return at < writer->count ? &writer->bodies[at] : NULL;
}
