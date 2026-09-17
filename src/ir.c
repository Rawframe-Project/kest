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

    [KEST_IR_REGION_OPEN] = {"region.open", KEST_IR_EFFECT_NONE},
    [KEST_IR_REGION_CLOSE] = {"region.close", KEST_IR_EFFECT_NONE},

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
// Whether a value of this type can hold anything the machine keeps. A number
// made inside a working-memory block is a number afterwards; a piece of text
// made there is a place that is not there any more.
static bool can_hold(const KestType *type) {
    const KestType *what = NULL;
    return kest_type_holds_own(type, &what);
}

// Whether what an operation leaves is made out of what it read rather than out
// of the heap. A cut of a piece of text is the piece it was cut from; an
// element read out of an array is the array's; a value out of its parts is its
// parts. Everything else that can hold what the machine keeps -- a call, a
// piece of text built, an array made -- is taken to have made it here, because
// what a called body did with the heap is not this body's to know.
static bool passes_through(uint16_t kind) {
    switch (kind) {
    case KEST_IR_CONST:
    case KEST_IR_CONST_AT:
    case KEST_IR_TRUE:
    case KEST_IR_FALSE:
    case KEST_IR_LOAD:
    case KEST_IR_ADDR:
    case KEST_IR_MAKE:
    case KEST_IR_PART:
    case KEST_IR_TURN:
    case KEST_IR_MEET:
    case KEST_IR_TEXT_SLICE:
    case KEST_IR_TEXT_REST:
    case KEST_IR_POP_LAST:
    case KEST_IR_TAKE:
    case KEST_IR_STORE_GET:
    case KEST_IR_STORE_REF:
        return true;
    default:
        return false;
    }
}

// Whether something can be written into a value of this type that holds what
// the machine keeps. A run of bytes cannot: copying a piece of text into one
// copies the bytes, and what the block made is gone with the block. A run of
// text can, and so can a store of a shape with text in it, and so can a
// reference to one. This is what tells a keep from a copy. See D966.
static bool can_keep(const KestType *type) {
    if (type == NULL) {
        return false;
    }
    switch (type->tag) {
    case KEST_T_ARRAY:
    case KEST_T_STORE:
    case KEST_T_REF:
    case KEST_T_OPTIONAL:
        return can_hold(type->element) || can_keep(type->element);
    case KEST_T_STRUCT:
        for (uint32_t i = 0; i < type->member_count; i++) {
            if (can_keep(type->members[i].type)) {
                return true;
            }
        }
        return false;
    case KEST_T_ENUM:
        for (uint32_t c = 0; c < type->case_count; c++) {
            for (uint32_t p = 0; p < type->cases[c].payload_count; p++) {
                if (can_keep(type->cases[c].payload[p])) {
                    return true;
                }
            }
        }
        return false;
    case KEST_T_FIXED:
        return can_hold(type->element) || can_keep(type->element);
    default:
        return false;
    }
}

static bool value_kept(const bool *made, const KestIrBody *body,
                       const KestIrOp *op, uint16_t which) {
    if (which >= op->arg_count) {
        return false;
    }
    KestIrRef ref = body->args[op->first_arg + which];
    return ref < body->value_count && made[ref];
}

// Whether an operation on a container asks the heap for more room. `fit` and
// `set` write into what is already there and are what a `scratch { }` block
// does to a thing that outlives it; `push`, `room` and `add` ask for more.
// See D972.
static bool grows_the_heap(KestIrKind kind) {
    switch (kind) {
    case KEST_IR_APPEND:
    case KEST_IR_ROOM:
    case KEST_IR_STORE_ADD:
        return true;
    default:
        return false;
    }
}

const char *kest_ir_escapes(const KestIrBody *body, KestArena *arena,
                            KestSpan *where) {
    bool *made = KEST_ARENA_ARRAY(arena, bool,
                                  body->value_count == 0 ? 1
                                                         : body->value_count);
    bool *kept = KEST_ARENA_ARRAY(arena, bool,
                                  body->slot_count == 0 ? 1 : body->slot_count);
    uint16_t inside[16];
    uint32_t open = 0;
    if (made == NULL || kept == NULL) {
        return "there was no room to follow what a block keeps";
    }
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        *where = op->span;
        if (op->kind == KEST_IR_REGION_OPEN) {
            if (open < sizeof(inside) / sizeof(inside[0])) {
                inside[open] = op->imm[1];
            }
            open++;
            continue;
        }
        if (op->kind == KEST_IR_REGION_CLOSE) {
            // A way out of the block rather than the end of it: the heap goes
            // back here and the block is still what a value made inside it
            // belongs to, so this one is not what stops the counting.
            if (op->imm[2] != 0) {
                continue;
            }
            if (open > 0) {
                open--;
                uint16_t from = open < sizeof(inside) / sizeof(inside[0])
                                    ? inside[open]
                                    : 0;
                for (uint16_t slot = from; slot < body->slot_count; slot++) {
                    kept[slot] = false;
                }
            }
            continue;
        }
        if (open == 0) {
            continue;
        }
        uint16_t nearest = inside[(open - 1) < 16 ? open - 1 : 15];
        const KestIrPlace *place =
            op->place == KEST_IR_NO_PLACE ? NULL : &body->places[op->place];

        // What a value is made of: something the block made, or something made
        // out of one. A comparison of two pieces of text is a truth and not a
        // piece of text, which is why the type is asked rather than the
        // operation.
        bool reads_one = false;
        for (uint16_t a = 0; a < op->arg_count; a++) {
            reads_one = reads_one || value_kept(made, body, op, a);
        }
        if (place != NULL && op->kind == KEST_IR_LOAD) {
            if (place->kind == KEST_IR_PLACE_SLOT) {
                for (uint16_t s = 0; s < place->slots; s++) {
                    uint32_t at = (uint32_t)place->slot + s;
                    reads_one = reads_one ||
                                (at < body->slot_count && kept[at]);
                }
            } else if (place->base < body->value_count) {
                reads_one = reads_one || made[place->base];
            }
        }
        if (op->dest != KEST_IR_NONE &&
            can_hold(body->values[op->dest].type) &&
            (reads_one || !passes_through(op->kind))) {
            made[op->dest] = true;
        }

        switch ((KestIrKind)op->kind) {
        case KEST_IR_PUT: {
            if (!value_kept(made, body, op, (uint16_t)(op->arg_count - 1))) {
                break;
            }
            if (place == NULL) {
                break;
            }
            if (place->kind == KEST_IR_PLACE_SLOT) {
                if (place->slot < nearest) {
                    return "this keeps what the block made in a name the "
                           "block does not own";
                }
                for (uint16_t s = 0; s < place->slots; s++) {
                    uint32_t at = (uint32_t)place->slot + s;
                    if (at < body->slot_count) {
                        kept[at] = true;
                    }
                }
                break;
            }
            if (place->base >= body->value_count || !made[place->base]) {
                return "this puts what the block made into something that "
                       "outlives it";
            }
            break;
        }
        // Everything a container is written through. The thing written into is
        // the first of what they read, and what goes in is the rest: a block's
        // own array may hold the block's own text, and nothing else may.
        case KEST_IR_APPEND:
        case KEST_IR_FIT:
        case KEST_IR_ROOM:
        case KEST_IR_STORE_ADD:
        case KEST_IR_STORE_SET: {
            bool into = value_kept(made, body, op, 0);
            // And growing one that is older than the block. The heap goes
            // back where it was when the block ends, and a container that
            // outlives the block would go back with it -- not the bytes it
            // was given inside, which nobody could reach anyway, but the ones
            // it already had, because a bump arena hands out what is next and
            // what is next is above the mark. A world grown inside a block
            // was emptied by the end of it, and nothing said so. So this is
            // refused where it is written: `fit` and `set` write into room a
            // thing already has and are what a block does, and `push`, `room`
            // and `add` ask for more and are not. See D972.
            if (!into && grows_the_heap((KestIrKind)op->kind)) {
                return "this grows something that outlives the block, and "
                       "what a block takes it gives back";
            }
            for (uint16_t a = 1; a < op->arg_count; a++) {
                if (value_kept(made, body, op, a) && !into) {
                    return "this puts what the block made into something that "
                           "outlives it";
                }
            }
            break;
        }
        case KEST_IR_GIVE:
            if (value_kept(made, body, op, 0)) {
                return "this gives back what the block made, and the block "
                       "puts it away";
            }
            break;
        // A call that is handed what the block made and something older to put
        // it in. What a called body does with what it is given is its own, so
        // this is the one shape that cannot be followed and is refused
        // instead. A crossing into the host is not one of these: see D966.
        case KEST_IR_CALL:
        case KEST_IR_CALL_VALUE: {
            bool any = false;
            for (uint16_t a = 0; a < op->arg_count; a++) {
                any = any || value_kept(made, body, op, a);
            }
            if (!any) {
                break;
            }
            for (uint16_t a = 0; a < op->arg_count; a++) {
                KestIrRef ref = body->args[op->first_arg + a];
                if (ref >= body->value_count || made[ref]) {
                    continue;
                }
                if (can_keep(body->values[ref].type)) {
                    return "this hands what the block made to something that "
                           "outlives it and could keep it";
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return NULL;
}

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
