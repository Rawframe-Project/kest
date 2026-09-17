#include "ir.h"

#include <string.h>

// One row an operation: what it is called where a reader sees it, and what it
// does that a promise is about. Everything that can be answered from the
// operation alone is answered here rather than at the places that ask, so that
// a backend, the contract proof and a reader are reading one list.
// `check-tables.sh` holds it to the enum beside it, name for name.
static const struct {
    const char *word;
    uint16_t effects;
} IR_OPS[] = {
    [KEST_IR_CONST] = {"const", KEST_IR_EFFECT_NONE},
    [KEST_IR_CONST_AT] = {"const.at", KEST_IR_EFFECT_NONE},
    [KEST_IR_TRUE] = {"true", KEST_IR_EFFECT_NONE},
    [KEST_IR_FALSE] = {"false", KEST_IR_EFFECT_NONE},
    [KEST_IR_LOAD] = {"load", KEST_IR_EFFECT_NONE},
    [KEST_IR_PUT] = {"put", KEST_IR_EFFECT_WRITES},
    [KEST_IR_ADDR] = {"addr", KEST_IR_EFFECT_NONE},
    [KEST_IR_MAKE] = {"make", KEST_IR_EFFECT_NONE},
    [KEST_IR_PART] = {"part", KEST_IR_EFFECT_NONE},
    [KEST_IR_TURN] = {"turn", KEST_IR_EFFECT_NONE},
    [KEST_IR_DROP] = {"drop", KEST_IR_EFFECT_NONE},

    [KEST_IR_ADD] = {"add", KEST_IR_EFFECT_NONE},
    [KEST_IR_SUB] = {"sub", KEST_IR_EFFECT_NONE},
    [KEST_IR_MUL] = {"mul", KEST_IR_EFFECT_NONE},
    [KEST_IR_DIV] = {"div", KEST_IR_EFFECT_NONE},
    [KEST_IR_MOD] = {"mod", KEST_IR_EFFECT_NONE},
    [KEST_IR_NEG] = {"neg", KEST_IR_EFFECT_NONE},
    [KEST_IR_AND] = {"and", KEST_IR_EFFECT_NONE},
    [KEST_IR_OR] = {"or", KEST_IR_EFFECT_NONE},
    [KEST_IR_XOR] = {"xor", KEST_IR_EFFECT_NONE},
    [KEST_IR_FLIP] = {"flip", KEST_IR_EFFECT_NONE},
    [KEST_IR_SHL] = {"shl", KEST_IR_EFFECT_NONE},
    [KEST_IR_SHR] = {"shr", KEST_IR_EFFECT_NONE},
    [KEST_IR_NARROW] = {"narrow", KEST_IR_EFFECT_NONE},
    [KEST_IR_TO_FLOAT] = {"to.float", KEST_IR_EFFECT_NONE},
    [KEST_IR_TO_WHOLE] = {"to.whole", KEST_IR_EFFECT_NONE},
    [KEST_IR_TO_F32] = {"to.f32", KEST_IR_EFFECT_NONE},

    [KEST_IR_LT] = {"lt", KEST_IR_EFFECT_NONE},
    [KEST_IR_LE] = {"le", KEST_IR_EFFECT_NONE},
    [KEST_IR_GT] = {"gt", KEST_IR_EFFECT_NONE},
    [KEST_IR_GE] = {"ge", KEST_IR_EFFECT_NONE},
    [KEST_IR_EQ] = {"eq", KEST_IR_EFFECT_NONE},
    [KEST_IR_NE] = {"ne", KEST_IR_EFFECT_NONE},
    [KEST_IR_NOT] = {"not", KEST_IR_EFFECT_NONE},
    [KEST_IR_HASH] = {"hash", KEST_IR_EFFECT_WEIGHED},

    [KEST_IR_TEXT_LEN] = {"text.len", KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_AT] = {"text.at", KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_IN] = {"text.in", KEST_IR_EFFECT_NONE},
    [KEST_IR_TEXT_SLICE] = {"text.slice",
                            KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_REST] = {"text.rest", KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_MATCHES] = {"text.matches", KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_FIND] = {"text.find", KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_OF] = {"text.of",
                         KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_JOIN] = {"text.join",
                           KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_TEXT_FROM] = {"text.from",
                           KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_WEIGHED},

    [KEST_IR_ARRAY] = {"array",
                       KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_ARRAY_NEW] = {"array.new",
                           KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_LEN] = {"len", KEST_IR_EFFECT_NONE},
    [KEST_IR_APPEND] = {"append", KEST_IR_EFFECT_ALLOCATES |
                                      KEST_IR_EFFECT_WRITES |
                                      KEST_IR_EFFECT_MOVES},
    [KEST_IR_FIT] = {"fit", KEST_IR_EFFECT_WRITES},
    [KEST_IR_ROOM] = {"room", KEST_IR_EFFECT_ALLOCATES |
                                  KEST_IR_EFFECT_WRITES |
                                  KEST_IR_EFFECT_MOVES},
    [KEST_IR_POP_LAST] = {"pop.last", KEST_IR_EFFECT_WRITES},
    [KEST_IR_TAKE] = {"take",
                      KEST_IR_EFFECT_WRITES | KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_CLEAR] = {"clear", KEST_IR_EFFECT_WRITES},

    [KEST_IR_STORE_NEW] = {"store.new",
                           KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_STORE_ADD] = {"store.add", KEST_IR_EFFECT_ALLOCATES |
                                            KEST_IR_EFFECT_WRITES |
                                            KEST_IR_EFFECT_MOVES},
    [KEST_IR_STORE_GET] = {"store.get", KEST_IR_EFFECT_NONE},
    [KEST_IR_STORE_SET] = {"store.set", KEST_IR_EFFECT_WRITES},
    [KEST_IR_STORE_REMOVE] = {"store.remove", KEST_IR_EFFECT_WRITES},
    [KEST_IR_STORE_COUNT] = {"store.count", KEST_IR_EFFECT_NONE},
    [KEST_IR_STORE_REF] = {"store.ref", KEST_IR_EFFECT_NONE},
    [KEST_IR_NEXT] = {"next", KEST_IR_EFFECT_NONE},
    [KEST_IR_SEEK_FROM] = {"seek.from", KEST_IR_EFFECT_WEIGHED},
    [KEST_IR_SEEK_NEXT] = {"seek.next", KEST_IR_EFFECT_WEIGHED},

    [KEST_IR_CALL] = {"call", KEST_IR_EFFECT_NONE},
    [KEST_IR_CALL_VALUE] = {"call.value", KEST_IR_EFFECT_NONE},
    [KEST_IR_CALL_HOST] = {"call.host",
                           KEST_IR_EFFECT_HOST | KEST_IR_EFFECT_UNSETTLED},

    [KEST_IR_GO] = {"go", KEST_IR_EFFECT_NONE},
    [KEST_IR_ASK] = {"ask", KEST_IR_EFFECT_NONE},
    [KEST_IR_GIVE] = {"give", KEST_IR_EFFECT_NONE},
    [KEST_IR_MEET] = {"meet", KEST_IR_EFFECT_NONE},
};

_Static_assert(sizeof(IR_OPS) / sizeof(IR_OPS[0]) == KEST_IR_OP_COUNT,
               "every operation is named and says what it does");

const char *kest_ir_word(KestIrKind kind) {
    return kind < KEST_IR_OP_COUNT ? IR_OPS[kind].word : "?";
}

static uint16_t kest_ir_effects(KestIrKind kind) {
    return kind < KEST_IR_OP_COUNT ? IR_OPS[kind].effects : 0;
}

// What each of a body's lists starts at, measured over the library and the
// examples the way a chunk's were in D753: the middle body is forty-one
// operations over six blocks with nine values and four places.
#define FLOOR_OPS 32
#define FLOOR_ARGS 32
#define FLOOR_VALUES 16
#define FLOOR_PLACES 8
#define FLOOR_NAMES 8
#define FLOOR_CONSTANTS 4

static void *grow(KestArena *arena, void *items, uint32_t count,
                  uint32_t *capacity, size_t size, uint32_t floor) {
    uint32_t grown = *capacity == 0 ? floor : *capacity * 2;
    void *moved = kest_arena_alloc(arena, size * grown, 16);
    if (moved == NULL) {
        return NULL;
    }
    if (count > 0) {
        memcpy(moved, items, size * count);
    }
    *capacity = grown;
    return moved;
}

void kest_ir_program_init(KestIrProgram *program, KestArena *arena,
                          KestIrWritten written, void *backend) {
    memset(program, 0, sizeof *program);
    program->arena = arena;
    program->written = written;
    program->backend = backend;
}

KestIrBody *kest_ir_body_begin(KestIrProgram *program) {
    program->before = kest_arena_mark(program->arena);
    memset(&program->body, 0, sizeof program->body);
    return &program->body;
}

bool kest_ir_body_end(KestIrProgram *program) {
    bool went = program->out_of_memory
                    ? false
                    : program->written(program->backend, &program->body);
    kest_arena_rewind(program->arena, program->before);
    memset(&program->body, 0, sizeof program->body);
    return went;
}

uint32_t kest_ir_place_add(KestIrProgram *program, KestIrBody *body,
                           const KestIrPlace *place) {
    if (body->place_count == body->place_capacity) {
        uint32_t capacity = body->place_capacity;
        void *places = grow(program->arena, body->places, body->place_count,
                            &capacity, sizeof(KestIrPlace), FLOOR_PLACES);
        if (places == NULL) {
            program->out_of_memory = true;
            return KEST_IR_NO_PLACE;
        }
        body->places = places;
        body->place_capacity = capacity;
    }
    body->places[body->place_count] = *place;
    return body->place_count++;
}

KestIrRef kest_ir_value_add(KestIrProgram *program, KestIrBody *body,
                            const KestType *type, uint16_t slots) {
    if (body->value_count == body->value_capacity) {
        uint32_t capacity = body->value_capacity;
        void *values = grow(program->arena, body->values, body->value_count,
                            &capacity, sizeof(KestIrValue), FLOOR_VALUES);
        if (values == NULL) {
            program->out_of_memory = true;
            return KEST_IR_NONE;
        }
        body->values = values;
        body->value_capacity = capacity;
    }
    KestIrValue *value = &body->values[body->value_count];
    value->type = type;
    value->slots = slots;
    value->made_by = body->op_count;
    value->read_by = KEST_IR_NONE;
    return body->value_count++;
}

uint32_t kest_ir_name_add(KestIrProgram *program, KestIrBody *body,
                          const KestIrName *name) {
    if (body->name_count == body->name_capacity) {
        uint32_t capacity = body->name_capacity;
        void *names = grow(program->arena, body->names, body->name_count,
                           &capacity, sizeof(KestIrName), FLOOR_NAMES);
        if (names == NULL) {
            program->out_of_memory = true;
            return 0;
        }
        body->names = names;
        body->name_capacity = capacity;
    }
    body->names[body->name_count] = *name;
    return body->name_count++;
}

uint32_t kest_ir_constants_add(KestIrProgram *program, KestIrBody *body,
                               const KestValue *values, const uint8_t *classes,
                               uint16_t count) {
    while (body->constant_count + count > body->constant_capacity) {
        uint32_t capacity = body->constant_capacity;
        void *constants =
            grow(program->arena, body->constants, body->constant_count,
                 &capacity, sizeof(KestValue), FLOOR_CONSTANTS);
        uint32_t for_classes = body->constant_capacity;
        void *held =
            grow(program->arena, body->constant_classes, body->constant_count,
                 &for_classes, sizeof(uint8_t), FLOOR_CONSTANTS);
        if (constants == NULL || held == NULL) {
            program->out_of_memory = true;
            return 0;
        }
        body->constants = constants;
        body->constant_classes = held;
        body->constant_capacity = capacity;
    }
    uint32_t first = body->constant_count;
    for (uint16_t i = 0; i < count; i++) {
        body->constants[first + i] = values[i];
        body->constant_classes[first + i] = classes[i];
    }
    body->constant_count += count;
    return first;
}

uint32_t kest_ir_op(KestIrProgram *program, KestIrBody *body, KestIrKind kind,
                    const KestType *type, const KestIrRef *args,
                    uint16_t arg_count, KestSpan span) {
    while (body->arg_count + arg_count > body->arg_capacity) {
        uint32_t capacity = body->arg_capacity;
        void *grown = grow(program->arena, body->args, body->arg_count,
                           &capacity, sizeof(KestIrRef), FLOOR_ARGS);
        if (grown == NULL) {
            program->out_of_memory = true;
            return 0;
        }
        body->args = grown;
        body->arg_capacity = capacity;
    }
    if (body->op_count == body->op_capacity) {
        uint32_t capacity = body->op_capacity;
        void *ops = grow(program->arena, body->ops, body->op_count, &capacity,
                         sizeof(KestIrOp), FLOOR_OPS);
        if (ops == NULL) {
            program->out_of_memory = true;
            return 0;
        }
        body->ops = ops;
        body->op_capacity = capacity;
    }
    uint32_t at = body->op_count++;
    KestIrOp *op = &body->ops[at];
    memset(op, 0, sizeof *op);
    op->kind = (uint16_t)kind;
    op->effects = kest_ir_effects(kind);
    op->dest = KEST_IR_NONE;
    op->place = KEST_IR_NO_PLACE;
    op->type = type;
    op->span = span;
    op->first_arg = body->arg_count;
    op->arg_count = arg_count;
    for (uint16_t i = 0; i < arg_count; i++) {
        body->args[body->arg_count++] = args[i];
        if (args[i] < body->value_count) {
            body->values[args[i]].read_by = at;
        }
    }
    return at;
}

void kest_ir_lands_here(KestIrBody *body, uint32_t branch) {
    if (branch < body->op_count) {
        body->ops[branch].target = body->op_count;
    }
}

// What a body has to be for a backend to read it without asking anything else.
// Said as a sentence rather than an index, because what a reader does with the
// answer is print it, and a number would send them back here.
const char *kest_ir_verify(const KestIrBody *body) {
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if (op->kind >= KEST_IR_OP_COUNT) {
            return "an operation this list has no name for";
        }
        if ((op->kind == KEST_IR_GO || op->kind == KEST_IR_ASK ||
             op->kind == KEST_IR_NEXT || op->kind == KEST_IR_SEEK_FROM ||
             op->kind == KEST_IR_SEEK_NEXT) &&
            op->target > body->op_count) {
            return "a branch landing on an operation this body has not got";
        }
        for (uint16_t a = 0; a < op->arg_count; a++) {
            KestIrRef ref = body->args[op->first_arg + a];
            if (ref >= body->value_count) {
                return "an operation reading a value this body has not made";
            }
            if (body->values[ref].made_by > i) {
                return "an operation reading a value made after it";
            }
        }
        if (op->place != KEST_IR_NO_PLACE && op->place >= body->place_count) {
            return "an operation naming a place this body has not got";
        }
    }
    for (uint32_t i = 0; i < body->value_count; i++) {
        if (body->values[i].read_by == KEST_IR_NONE) {
            return "a value nothing reads";
        }
    }
    return NULL;
}
