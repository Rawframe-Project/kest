#include "value.h"

#include <stdlib.h>
#include <string.h>

// The shortest spelling that reads back as the same number, so what is
// printed is what is there. A float with nothing after the point still gets
// one, because `3` and `3.0` are not the same value in this language.
int kest_write_real(char *buffer, size_t size, double value, bool narrow) {
    static const int WIDE[] = {6, 9, 12, 15, 17};
    static const int NARROW[] = {6, 9};
    const int *precisions = narrow ? NARROW : WIDE;
    size_t count = narrow ? 2 : 5;

    int written = 0;
    for (size_t i = 0; i < count; i++) {
        written = snprintf(buffer, size, "%.*g", precisions[i], value);
        double back = strtod(buffer, NULL);
        if (narrow ? (float)back == (float)value : back == value) {
            break;
        }
    }
    if (strpbrk(buffer, ".eni") == NULL) {
        written += snprintf(buffer + written, size - (size_t)written, ".0");
    }
    return written;
}

static void *grow(KestArena *arena, void *items, uint32_t count,
                  uint32_t *capacity, size_t size) {
    uint32_t grown = *capacity == 0 ? 32 : *capacity * 2;
    void *moved = kest_arena_alloc(arena, size * grown, 16);
    if (moved == NULL) {
        return NULL;
    }
    // The array starts out NULL, and memcpy is not allowed a null source
    // even for nothing.
    if (count > 0) {
        memcpy(moved, items, size * count);
    }
    *capacity = grown;
    return moved;
}

void kest_module_init(KestModule *module, KestArena *arena) {
    module->arena = arena;
    module->functions = NULL;
    module->count = 0;
    module->capacity = 0;
    module->externs = NULL;
    module->extern_count = 0;
    module->extern_capacity = 0;
    module->layouts = NULL;
    module->alias = "";
    module->layout_types = NULL;
    module->layout_count = 0;
    module->layout_capacity = 0;
}

KestChunk *kest_module_add(KestModule *module, const char *name) {
    if (module->count == module->capacity) {
        void *moved = grow(module->arena, module->functions, module->count,
                           &module->capacity, sizeof(KestChunk *));
        if (moved == NULL) {
            return NULL;
        }
        module->functions = moved;
    }

    KestChunk *chunk = KEST_ARENA_NEW(module->arena, KestChunk);
    if (chunk == NULL) {
        return NULL;
    }
    chunk->name = name;
    module->functions[module->count++] = chunk;
    return chunk;
}

int32_t kest_module_find(const KestModule *module, const char *name) {
    for (uint32_t i = 0; i < module->count; i++) {
        if (strcmp(module->functions[i]->name, name) == 0) {
            return (int32_t)i;
        }
    }

    // A host calls by name and should not have to know that a function is
    // compiled under what it takes as well. That works while the name means
    // one function, and when it means several the host has to say which.
    int32_t only = -1;
    return kest_module_copies(module, name, &only, 1) == 1 ? only : -1;
}

uint32_t kest_module_copies(const KestModule *module, const char *name,
                            int32_t *found, uint32_t room) {
    size_t length = strlen(name);
    uint32_t count = 0;
    for (uint32_t i = 0; i < module->count; i++) {
        const char *candidate = module->functions[i]->name;
        if (strncmp(candidate, name, length) != 0 || candidate[length] != '#') {
            continue;
        }
        if (count < room) {
            found[count] = (int32_t)i;
        }
        count++;
    }
    return count;
}

uint8_t kest_scalar_of(const KestType *type) {
    switch (type->tag) {
    case KEST_T_BOOL:
        return KEST_L_U8;
    case KEST_T_FLOAT:
        return type->width == 32 ? KEST_L_F32 : KEST_L_F64;
    // A set of bits is the unsigned integer it was declared over, which is
    // what a host reading the same memory sees.
    case KEST_T_FLAGS:
    case KEST_T_INT:
        switch (type->width) {
        case 8:
            return type->is_signed ? KEST_L_I8 : KEST_L_U8;
        case 16:
            return type->is_signed ? KEST_L_I16 : KEST_L_U16;
        case 32:
            return type->is_signed ? KEST_L_I32 : KEST_L_U32;
        default:
            return type->is_signed ? KEST_L_I64 : KEST_L_U64;
        }
    default:
        return KEST_L_WORD;
    }
}

// Whether anything in here is a tagged union, which is what makes the piece
// list not enough to move a value by.
static bool holds_a_tag(const KestType *type) {
    if (type == NULL) {
        return false;
    }
    if (type->tag == KEST_T_ENUM) {
        return true;
    }
    if (type->tag == KEST_T_OPTIONAL) {
        return holds_a_tag(type->element);
    }
    if (type->tag == KEST_T_FIXED) {
        return holds_a_tag(type->element);
    }
    if (type->tag == KEST_T_STRUCT) {
        for (uint32_t i = 0; i < type->member_count; i++) {
            if (holds_a_tag(type->members[i].type)) {
                return true;
            }
        }
    }
    return false;
}

// One piece per slot, in the order the slots are, each with where it is in
// memory. A nested struct contributes its own pieces at its own offset.
static uint16_t describe(KestPiece *pieces, uint16_t at, const KestType *type,
                         uint16_t base) {
    if (type == NULL) {
        pieces[at].offset = base;
        pieces[at].kind = KEST_L_WORD;
        return at + 1;
    }
    if (type->tag == KEST_T_STRUCT) {
        for (uint32_t i = 0; i < type->member_count; i++) {
            at = describe(pieces, at, type->members[i].type,
                          (uint16_t)(base + type->members[i].byte_offset));
        }
        return at;
    }
    // That many of the same thing, one after another, which is what a C array
    // inside a struct is.
    if (type->tag == KEST_T_FIXED) {
        for (uint32_t i = 0; i < type->count; i++) {
            at = describe(pieces, at, type->element,
                          (uint16_t)(base + i * type->element->byte_size));
        }
        return at;
    }
    // That many of the same thing, one after another, which is what a C array
    // inside a struct is.
    if (type->tag == KEST_T_FIXED) {
        for (uint32_t i = 0; i < type->count; i++) {
            at = describe(pieces, at, type->element,
                          (uint16_t)(base + i * type->element->byte_size));
        }
        return at;
    }
    if (type->tag == KEST_T_OPTIONAL) {
        at = describe(pieces, at, type->element, base);
        pieces[at].offset = (uint16_t)(base + type->element->byte_size);
        pieces[at].kind = KEST_L_U8;
        return at + 1;
    }
    // The tag, and then one slot per thing the widest case carries. What each
    // of those is depends on the tag, so they are placeholders and the moving
    // is done by type; the pieces are here so the count is the truth.
    if (type->tag == KEST_T_ENUM) {
        pieces[at].offset = base;
        pieces[at].kind = KEST_L_I32;
        at++;
        for (uint16_t s = 1; s < type->slots; s++) {
            pieces[at].offset = base;
            pieces[at].kind = KEST_L_WORD;
            at++;
        }
        return at;
    }
    pieces[at].offset = base;
    pieces[at].kind = kest_scalar_of(type);
    return at + 1;
}

// The name a program writes for a type is the last piece of the one it is
// registered under, and a host may write either.
static bool named_as(const KestType *type, const char *wanted) {
    const char *written = kest_type_written(type);
    if (written == NULL) {
        return false;
    }
    return strcmp(type->name, wanted) == 0 || strcmp(written, wanted) == 0;
}

const char *kest_module_nearest(const KestModule *module, const char *name) {
    size_t length = strlen(name);
    // The same rule the rest of the language suggests by: at one or two
    // characters everything is one edit from everything.
    if (length < 3) {
        return NULL;
    }
    uint32_t limit = length == 3 ? 1 : (uint32_t)length / 3;
    const KestType *best = NULL;
    uint32_t nearest = limit + 1;
    for (uint32_t i = 0; i < module->layout_count; i++) {
        const KestType *type = module->layout_types[i];
        const char *written = kest_type_written(type);
        if (written == NULL) {
            continue;
        }
        uint32_t distance = kest_edit_distance(name, length, written,
                                               strlen(written), limit);
        if (distance < nearest) {
            nearest = distance;
            best = type;
        }
    }
    return best == NULL ? NULL
                        : kest_module_askable(module, kest_type_written(best));
}

const char *kest_module_askable(const KestModule *module, const char *name) {
    const KestLayout *found[8];
    uint32_t count = kest_module_layout_of(module, name, found, 8);
    if (count <= 1) {
        return count == 0 ? NULL : name;
    }
    // Two of a name are told apart by the module in front of one, so that is
    // the only one of them a host can ask for and get.
    for (uint32_t i = 0; i < count && i < 8; i++) {
        const KestType *type = found[i]->type;
        if (type != NULL && type->name != NULL &&
            strchr(type->name, '.') != NULL) {
            return type->name;
        }
    }
    return NULL;
}

uint32_t kest_module_layout_of(const KestModule *module, const char *name,
                               const KestLayout **found, uint32_t room) {
    const KestType *last = NULL;
    uint32_t count = 0;
    for (uint32_t i = 0; i < module->layout_count; i++) {
        const KestType *type = module->layout_types[i];
        if (!named_as(type, name) || type == last) {
            continue;
        }
        last = type;
        if (count < room) {
            found[count] = &module->layouts[i];
        }
        count++;
    }
    return count;
}

int32_t kest_module_layout(KestModule *module, const KestType *type) {
    for (uint32_t i = 0; i < module->layout_count; i++) {
        if (module->layout_types[i] == type) {
            return (int32_t)i;
        }
    }
    if (module->layout_count == module->layout_capacity) {
        uint32_t capacity = module->layout_capacity;
        void *layouts = grow(module->arena, module->layouts,
                             module->layout_count, &capacity, sizeof(KestLayout));
        uint32_t types_capacity = module->layout_capacity;
        void *types =
            grow(module->arena, module->layout_types, module->layout_count,
                 &types_capacity, sizeof(const KestType *));
        if (layouts == NULL || types == NULL) {
            return -1;
        }
        module->layouts = layouts;
        module->layout_types = types;
        module->layout_capacity = capacity;
    }

    uint16_t slots = type == NULL || type->slots == 0 ? 1 : type->slots;
    KestPiece *pieces = KEST_ARENA_ARRAY(module->arena, KestPiece, slots);
    if (pieces == NULL) {
        return -1;
    }
    describe(pieces, 0, type, 0);

    KestLayout *layout = &module->layouts[module->layout_count];
    layout->pieces = pieces;
    layout->count = slots;
    layout->type = type;
    layout->tagged = holds_a_tag(type);
    layout->size = type == NULL || type->byte_size == 0 ? 8 : type->byte_size;
    layout->align = type == NULL || type->byte_align == 0 ? 8 : type->byte_align;
    module->layout_types[module->layout_count] = type;
    return (int32_t)module->layout_count++;
}

int32_t kest_module_extern(KestModule *module, const char *name, KestSpan span,
                           const KestSource *source) {
    for (uint32_t i = 0; i < module->extern_count; i++) {
        if (strcmp(module->externs[i].name, name) == 0) {
            return (int32_t)i;
        }
    }
    if (module->extern_count == module->extern_capacity) {
        void *moved =
            grow(module->arena, module->externs, module->extern_count,
                 &module->extern_capacity, sizeof(KestExtern));
        if (moved == NULL) {
            return -1;
        }
        module->externs = moved;
    }
    module->externs[module->extern_count].name = name;
    module->externs[module->extern_count].span = span;
    module->externs[module->extern_count].source = source;
    return (int32_t)module->extern_count++;
}

bool kest_chunk_emit(KestModule *module, KestChunk *chunk, uint8_t byte,
                     uint32_t origin) {
    if (chunk->code_count == chunk->code_capacity) {
        uint32_t capacity = chunk->code_capacity;
        void *code = grow(module->arena, chunk->code, chunk->code_count,
                          &capacity, sizeof(uint8_t));
        uint32_t origins_capacity = chunk->code_capacity;
        void *origins = grow(module->arena, chunk->origins, chunk->code_count,
                             &origins_capacity, sizeof(uint32_t));
        if (code == NULL || origins == NULL) {
            return false;
        }
        chunk->code = code;
        chunk->origins = origins;
        chunk->code_capacity = capacity;
    }
    chunk->origins[chunk->code_count] = origin;
    chunk->code[chunk->code_count++] = byte;
    return true;
}

bool kest_chunk_emit_u16(KestModule *module, KestChunk *chunk, uint16_t value,
                         uint32_t origin) {
    return kest_chunk_emit(module, chunk, (uint8_t)(value & 0xff), origin) &&
           kest_chunk_emit(module, chunk, (uint8_t)(value >> 8), origin);
}

uint32_t kest_chunk_constant(KestModule *module, KestChunk *chunk,
                             KestValue value, KestConstClass class) {
    // Constants are compared by their bits, so the same number written twice
    // is stored once. The class is part of the comparison because the integer
    // 0 and the float 0.0 have the same bits and are not the same constant to
    // a reader.
    for (uint32_t i = 0; i < chunk->constant_count; i++) {
        if (chunk->constant_classes[i] == class &&
            memcmp(&chunk->constants[i], &value, sizeof(KestValue)) == 0) {
            return i;
        }
    }
    if (chunk->constant_count == chunk->constant_capacity) {
        uint32_t capacity = chunk->constant_capacity;
        void *values = grow(module->arena, chunk->constants,
                            chunk->constant_count, &capacity, sizeof(KestValue));
        uint32_t class_capacity = chunk->constant_capacity;
        void *classes =
            grow(module->arena, chunk->constant_classes, chunk->constant_count,
                 &class_capacity, sizeof(uint8_t));
        if (values == NULL || classes == NULL) {
            return 0;
        }
        chunk->constants = values;
        chunk->constant_classes = classes;
        chunk->constant_capacity = capacity;
    }
    chunk->constant_classes[chunk->constant_count] = (uint8_t)class;
    chunk->constants[chunk->constant_count] = value;
    return chunk->constant_count++;
}

typedef enum {
    NONE,
    U16,
    U16_U16,
    U16_U16_U16,
    JUMP,
    BACK,
    STEP,
    WALK,
} Operands;

typedef struct {
    const char *name;
    Operands operands;
} Instruction;

static const Instruction INSTRUCTIONS[] = {
    {"const", U16},        {"load", U16},         {"store", U16},
    {"load.n", U16_U16},   {"store.n", U16_U16},  {"field", U16_U16_U16},
    {"array", U16_U16},    {"make.array", U16},   {"push", U16},
    {"index", U16},        {"pop.last", U16},     {"take", U16},
    {"clear", NONE},       {"elem.addr", U16},
    {"load.slots", U16_U16_U16},              {"store.slots", U16_U16_U16},
    {"offset.addr", U16_U16},
    {"load.at", U16_U16},  {"store.at", U16_U16}, {"len", NONE},
    {"text.len", NONE},    {"text.at", NONE},     {"text.slice", NONE},
    {"text.find", NONE},
    {"text.i", NONE},      {"text.u", NONE},      {"text.f", NONE},
    {"text.f32", NONE},    {"text.b", NONE},     {"text.flags", U16},
    {"text.enum", U16},
    {"concat", U16},
    {"hash.i", NONE},      {"hash.f", NONE},      {"hash.t", NONE},
    {"hash.enum", U16},    {"eq.enum", U16},      {"ne.enum", U16},
    {"text.from", NONE},
    {"new.store", U16},    {"add", U16},          {"get", U16},
    {"set", U16},          {"remove", NONE},      {"count", NONE},
    {"seek", NONE},        {"store.ref", NONE},
    {"true", NONE},        {"false", NONE},       {"pop", NONE},
    {"pop.n", U16},        {"dup", NONE},         {"rotate", U16},
    {"add.i", NONE},       {"sub.i", NONE},
    {"mul.i", NONE},       {"div.i", NONE},       {"mod.i", NONE},
    {"div.u", NONE},       {"mod.u", NONE},       {"neg.i", NONE},
    {"and.i", NONE},       {"or.i", NONE},        {"xor.i", NONE},
    {"not.i", NONE},       {"shl", NONE},         {"shr.i", NONE},
    {"shr.u", NONE},
    {"narrow", U16},       {"i2f", NONE},         {"u2f", NONE},
    {"f2i", U16},          {"to.f32", NONE},
    {"add.f", NONE},       {"sub.f", NONE},       {"mul.f", NONE},
    {"div.f", NONE},       {"neg.f", NONE},
    {"add.f32", NONE},     {"sub.f32", NONE},     {"mul.f32", NONE},
    {"div.f32", NONE},     {"neg.f32", NONE},     {"lt.i", NONE},
    {"le.i", NONE},        {"gt.i", NONE},        {"ge.i", NONE},
    {"lt.u", NONE},        {"le.u", NONE},        {"gt.u", NONE},
    {"ge.u", NONE},        {"lt.f", NONE},        {"le.f", NONE},
    {"gt.f", NONE},        {"ge.f", NONE},        {"eq.i", NONE},
    {"ne.i", NONE},        {"eq.f", NONE},        {"ne.f", NONE},
    {"eq.t", NONE},        {"ne.t", NONE},
    {"lt.t", NONE},        {"le.t", NONE},        {"gt.t", NONE},
    {"ge.t", NONE},        {"not", NONE},
    {"jump", JUMP},        {"jump.false", JUMP},  {"loop", BACK},
    {"next", STEP},        {"next.less.i", WALK}, {"next.less.u", WALK},
    {"call", U16_U16},     {"call.value", U16},
    {"call.host", U16_U16_U16},
    {"return", U16},
};

static uint16_t read_u16(const KestChunk *chunk, uint32_t offset) {
    return (uint16_t)(chunk->code[offset] | (chunk->code[offset + 1] << 8));
}

// How many bytes an instruction takes. This is the only place that knows, so
// a walk that prints and a walk that does not cannot come apart: D057's bug
// was a second answer to this question that had a jump seven bytes wide.
// How many bytes an instruction takes. Everything in this file that walks a
// chunk asks this and nothing works it out for itself, because two answers is
// how a walk goes out of step with the code. Nothing outside walks one; the
// day something does, this stops being static rather than being copied.
static uint32_t kest_op_width(uint8_t op) {
    switch (INSTRUCTIONS[op].operands) {
    case NONE:
        return 1;
    case U16:
    case JUMP:
    case BACK:
        // A jump carries how far as one number, printed as a place to make it
        // readable. It is the same two bytes.
        return 3;
    case U16_U16:
    case STEP:
        return 5;
    case U16_U16_U16:
    case WALK:
        return 7;
    }
    return 1;
}

// The deepest run of frames a call can make, and the slots those frames take
// together, written into `depth` and `slots` at this function's own place. A
// program that can reach itself has no answer and neither has one that calls
// through a value, because what a value points at is not known until it runs.
static bool measure_chunk(const KestModule *module, uint32_t which,
                          uint8_t *state, uint32_t *depth, uint32_t *slots,
                          KestReason *why) {
    if (state[which] == 2) {
        return true;
    }
    if (state[which] == 1) {
        // The one that comes back round, which is the one to name: it is where
        // the run of calls closes.
        why->reach = KEST_REACH_ITSELF;
        why->where = module->functions[which]->name;
        return false;
    }
    state[which] = 1;

    const KestChunk *chunk = module->functions[which];
    uint32_t deepest = 0;
    uint32_t widest = 0;
    for (uint32_t at = 0; at < chunk->code_count;) {
        uint8_t op = chunk->code[at];
        if (op == KEST_OP_CALL_VALUE) {
            why->reach = KEST_REACH_VALUE;
            why->where = chunk->name;
            state[which] = 0;
            return false;
        }
        if (op == KEST_OP_CALL) {
            uint16_t callee = read_u16(chunk, at + 1);
            if (callee >= module->count ||
                !measure_chunk(module, callee, state, depth, slots, why)) {
                state[which] = 0;
                return false;
            }
            if (depth[callee] > deepest) {
                deepest = depth[callee];
            }
            if (slots[callee] > widest) {
                widest = slots[callee];
            }
        }
        at += kest_op_width(op);
    }

    state[which] = 2;
    depth[which] = deepest + 1;
    slots[which] = widest + chunk->slot_count + chunk->stack_needed;
    return true;
}

// Whether an instruction reaches the heap. This is the list the machine
// itself keeps, read off the cases that call the allocator, and it is the one
// thing that makes a `no.alloc` promise a property of what runs rather than
// of what was read.
static bool op_allocates(uint8_t op) {
    switch (op) {
    case KEST_OP_ARRAY:
    case KEST_OP_MAKE_ARRAY:
    // Both of these can grow what they are given.
    case KEST_OP_PUSH:
    case KEST_OP_ADD:
    case KEST_OP_NEW_STORE:
    case KEST_OP_TEXT_SLICE:
    case KEST_OP_TEXT_I:
    case KEST_OP_TEXT_U:
    case KEST_OP_TEXT_F:
    case KEST_OP_TEXT_F32:
    case KEST_OP_TEXT_B:
    case KEST_OP_TEXT_FLAGS:
    case KEST_OP_TEXT_ENUM:
    case KEST_OP_CONCAT:
    case KEST_OP_TEXT_FROM:
        return true;
    default:
        return false;
    }
}

// Which chunk first reaches the heap, following calls, or -1. `where` is left
// at the instruction that does it.
static int32_t allocation_in(const KestModule *module, uint32_t which,
                             uint8_t *state, uint32_t *where) {
    if (state[which] != 0) {
        return -1;
    }
    state[which] = 1;

    const KestChunk *chunk = module->functions[which];
    for (uint32_t at = 0; at < chunk->code_count;) {
        uint8_t op = chunk->code[at];
        if (op_allocates(op)) {
            *where = at;
            return (int32_t)which;
        }
        if (op == KEST_OP_CALL) {
            uint16_t callee = read_u16(chunk, at + 1);
            if (callee < module->count) {
                int32_t found = allocation_in(module, callee, state, where);
                if (found >= 0) {
                    return found;
                }
            }
        }
        at += kest_op_width(op);
    }
    return -1;
}

bool kest_module_prove(const KestModule *module, KestArena *arena,
                       KestDiags *diags) {
    if (module->count == 0) {
        return true;
    }
    uint8_t *state = kest_arena_alloc(arena, module->count, 1);
    if (state == NULL) {
        return false;
    }

    bool held = true;

    // Every chunk has to be walkable, which means that stepping by what each
    // instruction says it takes lands exactly on the end. A width that is
    // wrong for one instruction puts everything after it out of step, and a
    // walk that reads the middle of an instruction as an instruction is how
    // half the calls in a program went unseen once (D057).
    uint32_t known = (uint32_t)(sizeof(INSTRUCTIONS) / sizeof(INSTRUCTIONS[0]));
    for (uint32_t i = 0; i < module->count; i++) {
        const KestChunk *chunk = module->functions[i];
        uint32_t at = 0;
        uint8_t last = KEST_OP_RETURN;
        const char *wrong = NULL;
        // A `return` may give back less than the function says, because the
        // one written past the end of a body gives nothing and is there for a
        // body that falls off it. More is what a host would read out of its
        // frame past the end, so it is the direction that is held.
        int32_t gives = -1;
        while (at < chunk->code_count) {
            last = chunk->code[at];
            if (last >= known) {
                wrong = "lands on something that is not an instruction";
                break;
            }
            if (last == KEST_OP_RETURN && gives < 0) {
                uint16_t count = read_u16(chunk, at + 1);
                if (count > chunk->result_slots) {
                    gives = count;
                }
            }
            at += kest_op_width(last);
        }
        if (wrong == NULL && at != chunk->code_count) {
            wrong = "steps past the end";
        }
        // Every chunk ends in a return, so a walk that ends anywhere else
        // stepped through the middle of something. Landing on the end by luck
        // is possible; landing on the end having last seen a return is not.
        if (wrong == NULL && last != KEST_OP_RETURN) {
            wrong = "ends on something that is not a return";
        }
        if (wrong != NULL) {
            KestSpan nowhere = {0, 0};
            kest_diags_in(diags, chunk->source);
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0406", nowhere,
                           "`%s` cannot be walked: %u bytes of code and a walk "
                           "that %s",
                           chunk->name, chunk->code_count, wrong);
            kest_diags_suggest(diags,
                               "an instruction is a different width from what "
                               "it says, which is a fault in the compiler");
            held = false;
        }
        // How wide a frame has to be is answered from the declaration before
        // anything runs, so a `return` wider than that would be read back into
        // a host's frame past the end of it.
        if (wrong == NULL && gives >= 0) {
            KestSpan nowhere = {0, 0};
            kest_diags_in(diags, chunk->source);
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0407", nowhere,
                           "`%s` has a `return` giving %d slots back where its "
                           "declaration gives %u",
                           chunk->name, gives, chunk->result_slots);
            kest_diags_suggest(diags,
                               "what a call reads back is the declaration's "
                               "width, which is a fault in the compiler");
            held = false;
        }
    }

    for (uint32_t i = 0; i < module->count; i++) {
        if (!module->functions[i]->no_alloc) {
            continue;
        }
        memset(state, 0, module->count);
        uint32_t where = 0;
        int32_t at = allocation_in(module, i, state, &where);
        if (at < 0) {
            continue;
        }
        // Reaching here means the walk over the tree missed something, so it
        // is reported against the instruction rather than against a promise:
        // the promise was checked and this is the code that was emitted for
        // it.
        const KestChunk *guilty = module->functions[at];
        KestSpan span = {guilty->origins[where], 1};
        // The name a program writes, not the one it was compiled under: what
        // a function takes is in its symbol and nobody wrote that.
        const char *symbol = module->functions[i]->name;
        const char *hash = strchr(symbol, '#');
        char written[128];
        size_t plain = hash == NULL ? strlen(symbol) : (size_t)(hash - symbol);
        if (plain >= sizeof(written)) {
            plain = sizeof(written) - 1;
        }
        memcpy(written, symbol, plain);
        written[plain] = '\0';

        kest_diags_in(diags, guilty->source);
        kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0405", span,
                       "this reaches the heap, and `%s` promises `no.alloc`",
                       written);
        kest_diags_suggest(diags,
                           "the promise was allowed and the code says "
                           "otherwise, which is a fault in the compiler");
        held = false;
    }
    return held;
}

bool kest_module_needs(const KestModule *module, KestArena *arena,
                       uint32_t *stack_slots, uint32_t *call_depth,
                       KestReason *why) {
    why->reach = KEST_REACH_KNOWN;
    why->where = NULL;
    if (module->count == 0) {
        *stack_slots = 0;
        *call_depth = 0;
        return true;
    }
    uint8_t *state = kest_arena_alloc(arena, module->count, 1);
    uint32_t *depth = KEST_ARENA_ARRAY(arena, uint32_t, module->count);
    uint32_t *slots = KEST_ARENA_ARRAY(arena, uint32_t, module->count);
    if (state == NULL || depth == NULL || slots == NULL) {
        why->reach = KEST_REACH_UNASKED;
        return false;
    }
    memset(state, 0, module->count);

    // A host may call anything the program defines, so the answer is the worst
    // of them.
    uint32_t worst_depth = 0;
    uint32_t worst_slots = 0;
    for (uint32_t i = 0; i < module->count; i++) {
        if (!measure_chunk(module, i, state, depth, slots, why)) {
            return false;
        }
        if (depth[i] > worst_depth) {
            worst_depth = depth[i];
        }
        if (slots[i] > worst_slots) {
            worst_slots = slots[i];
        }
    }
    *stack_slots = worst_slots;
    *call_depth = worst_depth;
    return true;
}

// Prints one instruction and says where the next one starts. What it prints is
// its own business; how far it moves is `kest_op_width` and nothing else.
static uint32_t disassemble_one(const KestChunk *chunk, uint32_t offset,
                                FILE *out) {
    uint8_t op = chunk->code[offset];
    const Instruction *instruction = &INSTRUCTIONS[op];
    fprintf(out, "  %04u  %-12s", offset, instruction->name);

    switch (instruction->operands) {
    case NONE:
        fputc('\n', out);
        break;
    case U16: {
        uint16_t operand = read_u16(chunk, offset + 1);
        if (op == KEST_OP_CONST) {
            KestValue value = chunk->constants[operand];
            switch (chunk->constant_classes[operand]) {
            case KEST_CONST_FLOAT:
                fprintf(out, "%u  ; %g\n", operand, value.real);
                break;
            case KEST_CONST_TEXT:
                fprintf(out, "%u  ; \"%s\"\n", operand, value.text);
                break;
            default:
                fprintf(out, "%u  ; %lld\n", operand,
                        (long long)value.integer);
            }
        } else {
            fprintf(out, "%u\n", operand);
        }
        break;
    }
    case U16_U16:
        fprintf(out, "%u  %u\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3));
        break;
    case U16_U16_U16:
        fprintf(out, "+%u  %u of %u\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3), read_u16(chunk, offset + 5));
        break;
    case JUMP:
        fprintf(out, "%u  -> %u\n", read_u16(chunk, offset + 1),
                offset + 3 + read_u16(chunk, offset + 1));
        break;
    case BACK:
        fprintf(out, "%u  -> %u\n", read_u16(chunk, offset + 1),
                offset + 3 - read_u16(chunk, offset + 1));
        break;
    case STEP:
        fprintf(out, "%u  -> %u\n", read_u16(chunk, offset + 1),
                offset + 5 - read_u16(chunk, offset + 3));
        break;
    case WALK:
        fprintf(out, "%u  < %u  -> %u\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3),
                offset + 7 - read_u16(chunk, offset + 5));
        break;
    }
    return offset + kest_op_width(op);
}

static const char *const SCALARS[] = {"i8",  "i16", "i32", "i64",
                                     "u8",  "u16", "u32", "u64",
                                     "f32", "f64", "word"};

void kest_module_disassemble(const KestModule *module, FILE *out) {
    // A file of nothing but generic functions has no bodies: a copy exists
    // where one is called, and nothing here called any.
    if (module->count == 0 && module->layout_count == 0 &&
        module->extern_count == 0) {
        fputs("nothing to run: every function here takes types, and a copy is "
              "compiled where one is called\n",
              out);
        return;
    }
    for (uint32_t i = 0; i < module->layout_count; i++) {
        const KestLayout *layout = &module->layouts[i];
        fprintf(out, "layout %u  %u byte%s aligned %u:", i, layout->size,
                layout->size == 1 ? "" : "s", layout->align);
        for (uint16_t p = 0; p < layout->count; p++) {
            fprintf(out, " +%u %s", layout->pieces[p].offset,
                    SCALARS[layout->pieces[p].kind]);
        }
        fputc('\n', out);
    }
    for (uint32_t i = 0; i < module->extern_count; i++) {
        fprintf(out, "host %s\n", module->externs[i].name);
    }

    for (uint32_t i = 0; i < module->count; i++) {
        const KestChunk *chunk = module->functions[i];
        fprintf(out, "fn %s  %u parameter slot%s, %u slot%s, %u deep\n",
                chunk->name, chunk->param_slots,
                chunk->param_slots == 1 ? "" : "s", chunk->slot_count,
                chunk->slot_count == 1 ? "" : "s", chunk->stack_needed);
        uint32_t offset = 0;
        while (offset < chunk->code_count) {
            offset = disassemble_one(chunk, offset, out);
        }
    }
}
