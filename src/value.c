#include "value.h"

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

// The shortest spelling that reads back as the same number, so what is
// printed is what is there. A float with nothing after the point still gets
// one, because `3` and `3.0` are not the same value in this language.
int kest_write_real(char *buffer, size_t size, double value, bool narrow) {
    // Not a number has no sign worth printing: which one comes out of a
    // divide is the machine's business and `-nan` says something about the
    // bits rather than about the value. It is also the one answer here that
    // cannot be read back, because there is no way to write it in the
    // language; an infinity is the same and keeps its sign, which does mean
    // something.
    if (value != value) {
        return snprintf(buffer, size, "nan");
    }
    // Every width from one up. The first that reads back is the shortest, and
    // a ladder that steps from six to nine prints nine digits for a number
    // that needed eight. Most numbers a program prints are short, so counting
    // up is where the answer usually is as well.
    int most = narrow ? 9 : 17;

    // Shortest is counted in characters and not in digits, because `%g` moves
    // to an exponent when the digits it is given run out: `123456792` reads
    // back at nine and at eight it is `1.2345679e+08`, which is fewer digits
    // and more to read. Once one is found without an exponent in it nothing
    // wider can be shorter, so that is where this stops.
    int chosen = most;
    size_t shortest = 0;
    for (int digits = 1; digits <= most; digits++) {
        int wrote = snprintf(buffer, size, "%.*g", digits, value);
        double back = strtod(buffer, NULL);
        if (narrow ? (float)back != (float)value : back != value) {
            continue;
        }
        if (shortest == 0 || (size_t)wrote < shortest) {
            shortest = (size_t)wrote;
            chosen = digits;
        }
        if (strchr(buffer, 'e') == NULL) {
            break;
        }
    }
    int written = snprintf(buffer, size, "%.*g", chosen, value);
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
    // One name, one function. What a function is compiled under carries what
    // tells it from the others of its name — what it takes, or what a copy was
    // given — so two of them here is this project having built one of those
    // names wrongly, and the second would quietly be the one that runs.
    for (uint32_t i = 0; i < module->count; i++) {
        if (strcmp(module->functions[i]->name, name) == 0) {
            return NULL;
        }
    }

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
    // And the same name as somebody wrote it, once. A name with nothing after
    // it is its own written form, so most functions share the one string.
    const char *hash = strchr(name, '#');
    chunk->wrote = hash == NULL
                       ? name
                       : kest_arena_strndup(module->arena, name,
                                            (size_t)(hash - name));
    if (chunk->wrote == NULL) {
        return NULL;
    }
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

// Which function a host means by a name. What a file writes is registered
// under the module it wrote it in, and a host writes what the file writes, so
// a bare name is looked for under the module of the file that was named as
// well. -1 when there is no such function.
int32_t kest_module_entry(const KestModule *module, const char *name) {
    int32_t found = kest_module_find(module, name);
    if (found >= 0) {
        return found;
    }
    const char *alias = module->alias;
    size_t prefix = alias == NULL ? 0 : strlen(alias);
    char qualified[256];
    if (prefix == 0 || prefix + strlen(name) + 2 > sizeof(qualified)) {
        return -1;
    }
    memcpy(qualified, alias, prefix);
    qualified[prefix] = '.';
    memcpy(qualified + prefix + 1, name, strlen(name) + 1);
    return kest_module_find(module, qualified);
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
    if (type->tag == KEST_T_OPTIONAL) {
        at = describe(pieces, at, type->element, base);
        pieces[at].offset = (uint16_t)(base + type->element->byte_size);
        pieces[at].kind = KEST_L_U8;
        return at + 1;
    }
    // The tag, and then one slot per thing the widest case carries. What each
    // of those is depends on the tag, so they are placeholders and the moving
    // is done by type; the pieces are here so the count is the truth.
    //
    // Where they sit is the widest case's, which is the case that decided how
    // big this is. They used to say the tag's own offset — three pieces of one
    // enum all at nought — and a host reading that would lay its own payload
    // over the tag.
    if (type->tag == KEST_T_ENUM) {
        pieces[at].offset = base;
        pieces[at].kind = KEST_L_I32;
        at++;

        const KestVariantType *widest = NULL;
        for (uint32_t c = 0; c < type->case_count; c++) {
            if (widest == NULL ||
                type->cases[c].payload_count > widest->payload_count) {
                widest = &type->cases[c];
            }
        }
        for (uint16_t s = 1; s < type->slots; s++) {
            uint32_t which = (uint32_t)s - 1;
            uint16_t where = widest != NULL && which < widest->payload_count
                                 ? widest->byte_offsets[which]
                                 : 4;
            pieces[at].offset = (uint16_t)(base + where);
            pieces[at].kind = KEST_L_PAYLOAD;
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
        uint32_t distance = kest_word_distance(name, length, written,
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
                           const KestSource *source, bool promises) {
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
    module->externs[module->extern_count].takes = NULL;
    module->externs[module->extern_count].takes_count = 0;
    module->externs[module->extern_count].gives = 0;
    module->externs[module->extern_count].gives_value = false;
    module->externs[module->extern_count].promises = promises;
    return (int32_t)module->extern_count++;
}

void kest_module_extern_shape(KestModule *module, uint32_t at, uint16_t *takes,
                              uint16_t count, uint16_t gives,
                              bool gives_value) {
    if (at >= module->extern_count) {
        return;
    }
    module->externs[at].takes = takes;
    module->externs[at].takes_count = count;
    module->externs[at].gives = gives;
    module->externs[at].gives_value = gives_value;
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

uint32_t kest_chunk_constant_run(KestModule *module, KestChunk *chunk,
                                 const KestValue *values,
                                 const uint8_t *classes, uint32_t count) {
    // Looked up as a run rather than a value at a time: the entries have to
    // be together and in order, so what is compared is the whole run. A table
    // read in ten places is stored once.
    for (uint32_t start = 0; count <= chunk->constant_count &&
                             start + count <= chunk->constant_count;
         start++) {
        bool same = true;
        for (uint32_t i = 0; i < count && same; i++) {
            same = chunk->constant_classes[start + i] == classes[i] &&
                   memcmp(&chunk->constants[start + i], &values[i],
                          sizeof(KestValue)) == 0;
        }
        if (same) {
            return start;
        }
    }

    uint32_t first = chunk->constant_count;
    for (uint32_t i = 0; i < count; i++) {
        if (chunk->constant_count == chunk->constant_capacity) {
            uint32_t capacity = chunk->constant_capacity;
            void *held =
                grow(module->arena, chunk->constants, chunk->constant_count,
                     &capacity, sizeof(KestValue));
            uint32_t class_capacity = chunk->constant_capacity;
            void *kinds = grow(module->arena, chunk->constant_classes,
                               chunk->constant_count, &class_capacity,
                               sizeof(uint8_t));
            if (held == NULL || kinds == NULL) {
                return 0;
            }
            chunk->constants = held;
            chunk->constant_classes = kinds;
            chunk->constant_capacity = capacity;
        }
        chunk->constant_classes[chunk->constant_count] = classes[i];
        chunk->constants[chunk->constant_count] = values[i];
        chunk->constant_count++;
    }
    return first;
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
    WALK,
    FIND,
    FIND_BACK,
} Operands;

typedef struct {
    const char *name;
    Operands operands;
} Instruction;

static const Instruction INSTRUCTIONS[] = {
    {"const", U16},        {"const.run", U16_U16},
    {"const.at", U16_U16_U16},        {"load", U16},         {"store", U16},
    {"load.n", U16_U16},   {"store.n", U16_U16},  {"field", U16_U16_U16},
    {"array", U16_U16},    {"make.array", U16},   {"push", U16},
    {"index", U16},        {"pop.last", U16},     {"take", U16},
    {"clear", NONE},       {"elem.addr", U16},
    {"load.slots", U16_U16_U16},              {"store.slots", U16_U16_U16},
    {"offset.addr", U16_U16},
    {"load.at", U16_U16},  {"store.at", U16_U16}, {"len", NONE},
    {"text.len", NONE},    {"text.at", NONE},     {"text.in", U16_U16},     {"text.slice", NONE},  {"text.rest", NONE},   {"text.matches", NONE},
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
    {"seek.from", FIND},   {"seek.next", FIND_BACK},        {"store.ref", NONE},
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
    {"jump", JUMP},        {"jump.false", JUMP},  {"jump.true", JUMP},
    {"jump.false.lt.i", JUMP}, {"jump.false.le.i", JUMP},
    {"jump.false.gt.i", JUMP}, {"jump.false.ge.i", JUMP},
    {"jump.false.eq.i", JUMP}, {"jump.false.ne.i", JUMP},
    {"jump.true.lt.i", JUMP}, {"jump.true.le.i", JUMP},
    {"jump.true.gt.i", JUMP}, {"jump.true.ge.i", JUMP},
    {"jump.true.eq.i", JUMP}, {"jump.true.ne.i", JUMP},
    {"jump.false.lt.f", JUMP}, {"jump.false.le.f", JUMP},
    {"jump.false.gt.f", JUMP}, {"jump.false.ge.f", JUMP},
    {"jump.false.eq.f", JUMP}, {"jump.false.ne.f", JUMP},
    {"jump.true.lt.f", JUMP}, {"jump.true.le.f", JUMP},
    {"jump.true.gt.f", JUMP}, {"jump.true.ge.f", JUMP},
    {"jump.true.eq.f", JUMP}, {"jump.true.ne.f", JUMP},
    {"loop", BACK},
{"next.less.i", WALK}, {"next.less.u", WALK},
    {"call", U16_U16},     {"call.value", U16},
    {"call.host", U16_U16_U16},
    {"return", U16},
};

// One name an opcode, and the compiler counts them, the same way the token
// names are counted. What each is called is `check-tables.sh`'s to hold.
_Static_assert(sizeof(INSTRUCTIONS) / sizeof(INSTRUCTIONS[0]) ==
                   KEST_OP_RETURN + 1,
               "every instruction has a name and nothing else does");

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
        return 5;
    case U16_U16_U16:
    case WALK:
    case FIND:
    case FIND_BACK:
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
                          uint32_t *host_depth, uint32_t *host_slots,
                          uint32_t *host_from, KestNoLeast *reasons,
                          KestReason *why) {
    if (state[which] == 2) {
        return true;
    }
    if (state[which] == 1) {
        // The one that comes back round, which is the one to name: it is where
        // the run of calls closes.
        why->reach = KEST_REACH_ITSELF;
        why->where = module->functions[which]->name;
        if (reasons != NULL) {
            reasons[which].reach = KEST_REACH_ITSELF;
            reasons[which].from = which;
        }
        return false;
    }
    state[which] = 1;

    const KestChunk *chunk = module->functions[which];
    uint32_t deepest = 0;
    uint32_t widest = 0;
    // The same two numbers again, over the runs of calls that end at a host
    // function rather than at a `return`. A host function is where a host may
    // call back in, and what it starts on top of is what is in use there.
    uint32_t host_deepest = 0;
    uint32_t host_widest = 0;
    bool reaches_host = false;
    // Which function the deepest call into the host is in, which is this one
    // until a callee turns out to reach one from further in. A host reads the
    // number to size a machine and the name to know what to shorten. See D605.
    uint32_t host_started = which;
    for (uint32_t at = 0; at < chunk->code_count;) {
        uint8_t op = chunk->code[at];
        if (op == KEST_OP_CALL_VALUE) {
            why->reach = KEST_REACH_VALUE;
            why->where = chunk->name;
            state[which] = 0;
            if (reasons != NULL) {
                reasons[which].reach = KEST_REACH_VALUE;
                reasons[which].from = which;
            }
            return false;
        }
        if (op == KEST_OP_CALL_HOST) {
            reaches_host = true;
        }
        if (op == KEST_OP_CALL) {
            uint16_t callee = read_u16(chunk, at + 1);
            if (callee >= module->count ||
                !measure_chunk(module, callee, state, depth, slots, host_depth,
                               host_slots, host_from, reasons, why)) {
                state[which] = 0;
                // A function that calls one with no answer has none either,
                // and for the same reason: what a reader asks about a function
                // is whether its own stack can be worked out, and it cannot if
                // anything it reaches has no bottom. See D601.
                if (reasons != NULL && callee < module->count) {
                    // And where it came from, which is what a reader opens: a
                    // function three calls above a `call.value` is told it
                    // calls through a value, and the one that does is the one
                    // to look at. See D602.
                    reasons[which].reach = reasons[callee].reach != 0
                                               ? reasons[callee].reach
                                               : (uint8_t)why->reach;
                    reasons[which].from = reasons[callee].reach != 0
                                              ? reasons[callee].from
                                              : callee;
                }
                return false;
            }
            if (depth[callee] > deepest) {
                deepest = depth[callee];
            }
            if (slots[callee] > widest) {
                widest = slots[callee];
            }
            if (host_depth[callee] > 0) {
                reaches_host = true;
            }
            if (host_depth[callee] > host_deepest) {
                host_deepest = host_depth[callee];
            }
            if (host_slots[callee] > host_widest) {
                host_widest = host_slots[callee];
                host_started = host_from[callee];
            }
        }
        at += kest_op_width(op);
    }

    state[which] = 2;
    uint32_t own = chunk->slot_count + chunk->stack_needed;
    depth[which] = deepest + 1;
    slots[which] = widest + own;
    // A chunk that reaches no host function is nought rather than its own
    // width: what this measures is where a call into the host happens, and one
    // that never happens is not a place.
    host_depth[which] = reaches_host ? host_deepest + 1 : 0;
    host_slots[which] = reaches_host ? host_widest + own : 0;
    host_from[which] = host_started;
    return true;
}

// Whether an instruction reaches the heap. This is the list the machine
// itself keeps, read off the cases that call the allocator, and it is the one
// thing that makes a `no.alloc` promise a property of what runs rather than
// of what was read.
static bool op_allocates(uint8_t op) {
    // Every instruction is named, and none of them falls into a `default`: an
    // instruction added to the language that reaches the heap would otherwise
    // be one this proof does not know about, and a promise kept by not
    // looking. The walk over the tree is the first proof of a `no.alloc`
    // promise and this is the second; the second is what says the first was
    // wrong, so it cannot be the one that is quietly out of date.
    switch ((KestOp)op) {
    case KEST_OP_ARRAY:
    case KEST_OP_MAKE_ARRAY:
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
    case KEST_OP_CONST:
    case KEST_OP_CONST_RUN:
    case KEST_OP_CONST_AT:
    case KEST_OP_LOAD:
    case KEST_OP_STORE:
    case KEST_OP_LOADN:
    case KEST_OP_STOREN:
    case KEST_OP_FIELD:
    case KEST_OP_INDEX:
    case KEST_OP_POP_LAST:
    case KEST_OP_TAKE:
    case KEST_OP_CLEAR:
    case KEST_OP_ELEM_ADDR:
    case KEST_OP_LOAD_SLOTS:
    case KEST_OP_STORE_SLOTS:
    case KEST_OP_OFFSET_ADDR:
    case KEST_OP_LOAD_AT:
    case KEST_OP_STORE_AT:
    case KEST_OP_LEN:
    case KEST_OP_TEXT_LEN:
    case KEST_OP_TEXT_AT:
    case KEST_OP_TEXT_IN:
    case KEST_OP_TEXT_REST:
    case KEST_OP_TEXT_MATCHES:
    case KEST_OP_TEXT_FIND:
    case KEST_OP_HASH_I:
    case KEST_OP_HASH_F:
    case KEST_OP_HASH_T:
    case KEST_OP_HASH_ENUM:
    case KEST_OP_EQ_ENUM:
    case KEST_OP_NE_ENUM:
    case KEST_OP_GET:
    case KEST_OP_SET:
    case KEST_OP_REMOVE:
    case KEST_OP_COUNT:
    case KEST_OP_SEEK_FROM:
    case KEST_OP_SEEK_NEXT:
    case KEST_OP_STORE_REF:
    case KEST_OP_TRUE:
    case KEST_OP_FALSE:
    case KEST_OP_POP:
    case KEST_OP_POPN:
    case KEST_OP_DUP:
    case KEST_OP_ROTATE:
    case KEST_OP_ADD_I:
    case KEST_OP_SUB_I:
    case KEST_OP_MUL_I:
    case KEST_OP_DIV_I:
    case KEST_OP_MOD_I:
    case KEST_OP_DIV_U:
    case KEST_OP_MOD_U:
    case KEST_OP_NEG_I:
    case KEST_OP_AND_I:
    case KEST_OP_OR_I:
    case KEST_OP_XOR_I:
    case KEST_OP_NOT_I:
    case KEST_OP_SHL:
    case KEST_OP_SHR_I:
    case KEST_OP_SHR_U:
    case KEST_OP_NARROW:
    case KEST_OP_I2F:
    case KEST_OP_U2F:
    case KEST_OP_F2I:
    case KEST_OP_TO_F32:
    case KEST_OP_ADD_F:
    case KEST_OP_SUB_F:
    case KEST_OP_MUL_F:
    case KEST_OP_DIV_F:
    case KEST_OP_NEG_F:
    case KEST_OP_ADD_F32:
    case KEST_OP_SUB_F32:
    case KEST_OP_MUL_F32:
    case KEST_OP_DIV_F32:
    case KEST_OP_NEG_F32:
    case KEST_OP_LT_I:
    case KEST_OP_LE_I:
    case KEST_OP_GT_I:
    case KEST_OP_GE_I:
    case KEST_OP_LT_U:
    case KEST_OP_LE_U:
    case KEST_OP_GT_U:
    case KEST_OP_GE_U:
    case KEST_OP_LT_F:
    case KEST_OP_LE_F:
    case KEST_OP_GT_F:
    case KEST_OP_GE_F:
    case KEST_OP_EQ_I:
    case KEST_OP_NE_I:
    case KEST_OP_EQ_F:
    case KEST_OP_NE_F:
    case KEST_OP_EQ_T:
    case KEST_OP_NE_T:
    case KEST_OP_LT_T:
    case KEST_OP_LE_T:
    case KEST_OP_GT_T:
    case KEST_OP_GE_T:
    case KEST_OP_NOT:
    case KEST_OP_JUMP:
    case KEST_OP_JUMP_FALSE:
    case KEST_OP_JUMP_TRUE:
    case KEST_OP_JUMP_FALSE_LT_I:
    case KEST_OP_JUMP_FALSE_LE_I:
    case KEST_OP_JUMP_FALSE_GT_I:
    case KEST_OP_JUMP_FALSE_GE_I:
    case KEST_OP_JUMP_FALSE_EQ_I:
    case KEST_OP_JUMP_FALSE_NE_I:
    case KEST_OP_JUMP_TRUE_LT_I:
    case KEST_OP_JUMP_TRUE_LE_I:
    case KEST_OP_JUMP_TRUE_GT_I:
    case KEST_OP_JUMP_TRUE_GE_I:
    case KEST_OP_JUMP_TRUE_EQ_I:
    case KEST_OP_JUMP_TRUE_NE_I:
    case KEST_OP_JUMP_FALSE_LT_F:
    case KEST_OP_JUMP_FALSE_LE_F:
    case KEST_OP_JUMP_FALSE_GT_F:
    case KEST_OP_JUMP_FALSE_GE_F:
    case KEST_OP_JUMP_FALSE_EQ_F:
    case KEST_OP_JUMP_FALSE_NE_F:
    case KEST_OP_JUMP_TRUE_LT_F:
    case KEST_OP_JUMP_TRUE_LE_F:
    case KEST_OP_JUMP_TRUE_GT_F:
    case KEST_OP_JUMP_TRUE_GE_F:
    case KEST_OP_JUMP_TRUE_EQ_F:
    case KEST_OP_JUMP_TRUE_NE_F:
    case KEST_OP_LOOP:
    case KEST_OP_NEXT_LESS_I:
    case KEST_OP_NEXT_LESS_U:
    case KEST_OP_CALL:
    case KEST_OP_CALL_VALUE:
    case KEST_OP_CALL_HOST:
    case KEST_OP_RETURN:
        return false;
    }
    return false;
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
            kest_diags_fault(diags,
                             "an instruction is a different width from what "
                             "it says");
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
            kest_diags_fault(diags,
                             "what a call reads back is the declaration's "
                             "width");
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
        const char *written = module->functions[i]->wrote;

        kest_diags_in(diags, guilty->source);
        kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0405", span,
                       "this reaches the heap, and `%s` promises `no.alloc`",
                       written);
        kest_diags_fault(diags,
                         "the promise was allowed and the code says "
                         "otherwise");
        held = false;
    }
    return held;
}

bool kest_module_needs(const KestModule *module, KestArena *arena,
                       int32_t only, uint32_t *stack_slots,
                       uint32_t *call_depth, uint32_t *from_host_slots,
                       uint32_t *from_host_frames, KestNoLeast *reasons,
                       KestReason *why) {
    why->reach = KEST_REACH_KNOWN;
    why->where = NULL;
    if (from_host_slots != NULL) {
        *from_host_slots = 0;
    }
    if (from_host_frames != NULL) {
        *from_host_frames = 0;
    }
    if (module->count == 0) {
        *stack_slots = 0;
        *call_depth = 0;
        return true;
    }
    uint8_t *state = kest_arena_alloc(arena, module->count, 1);
    uint32_t *depth = KEST_ARENA_ARRAY(arena, uint32_t, module->count);
    uint32_t *slots = KEST_ARENA_ARRAY(arena, uint32_t, module->count);
    uint32_t *host_depth = KEST_ARENA_ARRAY(arena, uint32_t, module->count);
    uint32_t *host_slots = KEST_ARENA_ARRAY(arena, uint32_t, module->count);
    uint32_t *host_from = KEST_ARENA_ARRAY(arena, uint32_t, module->count);
    if (state == NULL || depth == NULL || slots == NULL ||
        host_depth == NULL || host_slots == NULL || host_from == NULL) {
        // Not that there is no answer: a host that frees something and asks
        // again may be told one, and a host told nothing was asked would not
        // know to. See D566.
        why->reach = KEST_REACH_NO_ROOM;
        return false;
    }
    memset(state, 0, module->count);

    // A host may call anything the program defines, so the answer is the worst
    // of them — unless a host says which one it calls, and then the answer is
    // that one and what it reaches. A host that knows is not made to pay for
    // what it will never call.
    uint32_t worst_depth = 0;
    uint32_t worst_slots = 0;
    uint32_t worst_host_depth = 0;
    uint32_t worst_host_slots = 0;
    uint32_t started_at = 0;
    uint32_t from = only < 0 ? 0 : (uint32_t)only;
    uint32_t until = only < 0 ? module->count : from + 1;
    if (from >= module->count) {
        why->reach = KEST_REACH_NO_NAME;
        return false;
    }
    // The first function with no answer is the one the program is told about,
    // and it is not the only one there is: a caller of it has no answer
    // either, and a walk that stopped at the first would leave every other one
    // looking as if it had been worked out. So when somebody wants them all,
    // the walk carries on and the first reason is kept. See D601.
    bool answered = true;
    KestReason first = {KEST_REACH_KNOWN, NULL};
    for (uint32_t i = from; i < until; i++) {
        if (!measure_chunk(module, i, state, depth, slots, host_depth,
                           host_slots, host_from, reasons, why)) {
            if (answered) {
                first = *why;
                answered = false;
            }
            if (reasons == NULL) {
                return false;
            }
            continue;
        }
        if (depth[i] > worst_depth) {
            worst_depth = depth[i];
        }
        if (slots[i] > worst_slots) {
            worst_slots = slots[i];
        }
        if (host_depth[i] > worst_host_depth) {
            worst_host_depth = host_depth[i];
        }
        if (host_slots[i] > worst_host_slots) {
            worst_host_slots = host_slots[i];
            started_at = host_from[i];
        }
    }
    // And what each of them needs on its own, which the walk above worked out
    // for every function it reached: a host that calls one function is sized
    // for that one rather than for the worst there is, and it does not have to
    // ask about it by name to be told. See D603.
    if (reasons != NULL) {
        for (uint32_t i = 0; i < module->count; i++) {
            if (state[i] == 2) {
                reasons[i].slots = slots[i];
                reasons[i].frames = depth[i];
            }
        }
    }
    if (!answered) {
        *why = first;
        return false;
    }
    *stack_slots = worst_slots;
    *call_depth = worst_depth;
    if (from_host_slots != NULL) {
        *from_host_slots = worst_host_slots;
        // And the function it is in, in the field that says which function an
        // answer is about. A host reads the number to size a machine; the name
        // is what it would have to shorten to make the number smaller, and
        // nothing said it. See D605.
        if (worst_host_slots > 0 && started_at < module->count) {
            why->where = module->functions[started_at]->name;
        }
    }
    if (from_host_frames != NULL) {
        *from_host_frames = worst_host_depth;
    }
    return true;
}

// Prints one instruction and says where the next one starts. What it prints is
// its own business; how far it moves is `kest_op_width` and nothing else.
static uint32_t disassemble_one(const KestModule *module,
                                const KestChunk *chunk, uint32_t offset,
                                FILE *out) {
    uint8_t op = chunk->code[offset];
    const Instruction *instruction = &INSTRUCTIONS[op];
    // Wide enough for the longest name there is, so a number after one never
    // runs into it.
    fprintf(out, "  %04u  %-16s", offset, instruction->name);

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
        // The name of what a call reaches, beside the number that reaches it.
        // Every other instruction that names something says what it named —
        // a constant prints its value — and this one printed an index into a
        // list a reader would have to count. What calls what is the one thing
        // the emitted code already knows and nobody could read. See D599.
        if (op == KEST_OP_CALL && module != NULL &&
            read_u16(chunk, offset + 1) < module->count) {
            fprintf(out, "%u  %u  ; %s\n", read_u16(chunk, offset + 1),
                    read_u16(chunk, offset + 3),
                    module->functions[read_u16(chunk, offset + 1)]->name);
            break;
        }
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
    case WALK:
        fprintf(out, "%u  < %u  -> %u\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3),
                offset + 7 - read_u16(chunk, offset + 5));
        break;
    case FIND:
        fprintf(out, "%u  %u  -> %u\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3),
                offset + 7 + read_u16(chunk, offset + 5));
        break;
    case FIND_BACK:
        fprintf(out, "%u  %u  -> %u\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3),
                offset + 7 - read_u16(chunk, offset + 5));
        break;
    }
    return offset + kest_op_width(op);
}

static const char *const SCALARS[] = {"i8",  "i16", "i32", "i64",
                                     "u8",  "u16", "u32", "u64",
                                     "f32", "f64", "word", "payload"};

// A reason built where it is kept, because it names the type the word did not
// fit in (D193).
static const char *reason_of(KestArena *arena, const char *format, ...) {
    va_list args;
    va_list again;
    va_start(args, format);
    va_copy(again, args);
    int room = vsnprintf(NULL, 0, format, args);
    va_end(args);
    char *out = room < 0 ? NULL : kest_arena_alloc(arena, (size_t)room + 1, 1);
    if (out != NULL) {
        vsnprintf(out, (size_t)room + 1, format, again);
    }
    va_end(again);
    return out == NULL ? "does not fit" : out;
}

// The ends of a whole number of that width and sign. `u64` is cut short at
// what a signed read can carry, which is the widest thing the shell can hand
// over anyway.
static void ends_of(const KestType *type, long long *low, long long *high) {
    if (type->is_signed) {
        *high = type->width >= 64 ? LLONG_MAX
                                  : ((long long)1 << (type->width - 1)) - 1;
        *low = -*high - 1;
        return;
    }
    *low = 0;
    *high = type->width >= 63 ? LLONG_MAX
                              : ((long long)1 << type->width) - 1;
}

bool kest_value_read(KestArena *arena, const char *text, const KestType *type,
                     KestValue *into, const char **why) {
    char *end = NULL;
    switch (type->tag) {
    case KEST_T_INT: {
        errno = 0;
        long long value = strtoll(text, &end, 0);
        if (end == text || *end != '\0') {
            *why = "is not a number";
            return false;
        }
        long long low = 0;
        long long high = 0;
        ends_of(type, &low, &high);
        // A number the machine cannot carry, and one it can carry and the
        // type cannot hold. Both were taken before, and what the program got
        // was a number nobody typed.
        if (errno == ERANGE || value < low || value > high) {
            *why = reason_of(arena, "does not fit in `%s`",
                             kest_type_name(arena, type));
            return false;
        }
        into->integer = value;
        return true;
    }
    case KEST_T_FLOAT: {
        errno = 0;
        double value = strtod(text, &end);
        if (end == text || *end != '\0') {
            *why = "is not a number";
            return false;
        }
        if (errno == ERANGE && value != 0.0) {
            *why = reason_of(arena, "does not fit in `%s`",
                             kest_type_name(arena, type));
            return false;
        }
        if (type->width == 32 && value > (double)FLT_MAX) {
            *why = reason_of(arena, "does not fit in `%s`",
                             kest_type_name(arena, type));
            return false;
        }
        into->real = type->width == 32 ? (double)(float)value : value;
        return true;
    }
    case KEST_T_BOOL:
        if (strcmp(text, "true") == 0 || strcmp(text, "false") == 0) {
            into->integer = strcmp(text, "true") == 0;
            return true;
        }
        *why = "is not `true` or `false`";
        return false;
    case KEST_T_TEXT:
        into->text = text;
        return true;
    default:
        *why = "cannot be written as a word";
        return false;
    }
}

const char *kest_scalar_name(uint8_t kind) {
    // One list, so a message about what a slot holds and a walk that prints a
    // layout cannot come to call the same thing two names.
    return kind < sizeof(SCALARS) / sizeof(SCALARS[0]) ? SCALARS[kind]
                                                       : "something else";
}

_Static_assert(sizeof(SCALARS) / sizeof(SCALARS[0]) == KEST_L_PAYLOAD + 1,
               "every scalar a layout holds has a name and nothing else does");

// What a reason there is no least is called, which the JSON and the words a
// listing prints are the same list of: a reason added to `KestReach` is caught
// here rather than printed as whatever the last one fell through to.
const char *kest_reach_name(KestReach reach) {
    switch (reach) {
    case KEST_REACH_KNOWN:
        return "worked out";
    case KEST_REACH_ITSELF:
        return "reaches itself";
    case KEST_REACH_VALUE:
        return "calls through a value";
    case KEST_REACH_NO_NAME:
        return "no function of that name";
    case KEST_REACH_NO_ROOM:
        return "no room to work it out";
    case KEST_REACH_UNASKED:
        return "not worked out";
    }
    // Not reached while those are the reasons there are, and the switch above
    // is what holds them to being all of them.
    return "not worked out";
}

void kest_module_needs_json(const KestModule *module, int32_t only,
                            FILE *out) {
    uint32_t stack = 0;
    uint32_t deep = 0;
    KestReason why = {KEST_REACH_UNASKED, NULL};
    if (kest_module_needs(module, module->arena, only, &stack, &deep, NULL,
                          NULL, NULL, &why)) {
        fprintf(out, "\"slots\":%u,\"frames\":%u", stack, deep);
        return;
    }
    fputs("\"slots\":null,\"frames\":null,\"why\":", out);
    kest_json_text(kest_reach_name(why.reach), out);
    fputs(",\"where\":", out);
    if (why.where == NULL) {
        fputs("null", out);
    } else {
        kest_json_text(why.where, out);
    }
}

void kest_module_disassemble_json(const KestModule *module,
                                  const char *const *entries, FILE *out) {
    fputs("\"layouts\":[", out);
    for (uint32_t i = 0; i < module->layout_count; i++) {
        const KestLayout *layout = &module->layouts[i];
        // `tagged` is what the C side of this carries and the JSON did not:
        // a host reading pieces has to know whether they are pieces it may
        // walk or a payload it has to switch on.
        fprintf(out, "%s{\"bytes\":%u,\"align\":%u,\"tagged\":%s,"
                     "\"pieces\":[",
                i == 0 ? "" : ",", layout->size, layout->align,
                layout->tagged ? "true" : "false");
        for (uint16_t p = 0; p < layout->count; p++) {
            fprintf(out, "%s{\"byte\":%u,\"is\":\"%s\"}", p == 0 ? "" : ",",
                    layout->pieces[p].offset,
                    SCALARS[layout->pieces[p].kind]);
        }
        fputs("]}", out);
    }

    fputs("],\"hosts\":[", out);
    for (uint32_t i = 0; i < module->extern_count; i++) {
        fputs(i == 0 ? "" : ",", out);
        kest_json_text(module->externs[i].name, out);
    }

    // The same two numbers the text form prints, and null where there are
    // none to give: a run of calls that comes back round has no deepest frame
    // and a call through a value reaches what is not known until it runs, so
    // `why` says which of the two it was.
    fputs("],\"needs\":{", out);
    kest_module_needs_json(module, -1, out);

    // And one for each name the caller asked about that the program has,
    // whether or not it differs from the whole. The text form leaves out the
    // ones that are the same because a reader would be reading them twice; a
    // tool looks one up by name and wants it there.
    fputs(",\"entries\":[", out);
    bool first_entry = true;
    for (uint32_t e = 0; entries != NULL && entries[e] != NULL; e++) {
        int32_t at = kest_module_entry(module, entries[e]);
        if (at < 0) {
            continue;
        }
        fputs(first_entry ? "{\"name\":" : ",{\"name\":", out);
        first_entry = false;
        kest_json_text(entries[e], out);
        fputc(',', out);
        kest_module_needs_json(module, at, out);
        fputc('}', out);
    }
    fputs("]}", out);

    // The same walk the words make, for the same reason: which function the
    // answer stopped at is a thing about that function and is said beside it.
    uint32_t reached = 0;
    uint32_t deep = 0;
    KestReason why = {KEST_REACH_UNASKED, NULL};
    // One byte a function, which is what saying it about every one of them
    // costs: the walk visits them all anyway, and a reason it does not write
    // down is a function that looks as if it had been worked out. See D601.
    KestNoLeast *reasons =
        module->count == 0
            ? NULL
            : KEST_ARENA_ARRAY(module->arena, KestNoLeast, module->count);
    if (reasons != NULL) {
        memset(reasons, 0, sizeof(KestNoLeast) * module->count);
    }
    (void)kest_module_needs(module, module->arena, -1, &reached, &deep, NULL,
                            NULL, reasons, &why);

    fputs(",\"functions\":[", out);
    for (uint32_t i = 0; i < module->count; i++) {
        const KestChunk *chunk = module->functions[i];
        fputs(i == 0 ? "" : ",", out);
        fputs("{\"name\":", out);
        kest_json_text(chunk->name, out);
        // And what it was written as, which is the name a declaration has and
        // this one has what it was compiled with on the end of. A tool reading
        // a listing beside `check` joins the two on it; cutting the name at
        // the `#` is the same rule written a second time, in whoever is
        // reading it. The words form says it as the front of the name, which
        // is where a person reads it. See D611.
        fputs(",\"wrote\":", out);
        kest_json_text(chunk->wrote, out);
        fprintf(out,
                ",\"parameterSlots\":%u,\"slots\":%u,\"deep\":%u"
                ",\"noAlloc\":%s,\"why\":",
                chunk->param_slots, chunk->slot_count, chunk->stack_needed,
                chunk->no_alloc ? "true" : "false");
        if (reasons != NULL && reasons[i].reach != 0) {
            kest_json_text(kest_reach_name((KestReach)reasons[i].reach), out);
            fputs(",\"where\":", out);
            kest_json_text(module->functions[reasons[i].from]->name, out);
            fputs(",\"least\":null", out);
        } else if (reasons != NULL) {
            fprintf(out, "null,\"where\":null,\"least\":{\"slots\":%u,"
                         "\"frames\":%u}",
                    reasons[i].slots, reasons[i].frames);
        } else {
            fputs("null,\"where\":null,\"least\":null", out);
        }
        fputs(",\"code\":[", out);
        uint32_t offset = 0;
        bool first = true;
        while (offset < chunk->code_count) {
            uint8_t op = chunk->code[offset];
            fputs(first ? "" : ",", out);
            first = false;
            fputs("{\"at\":", out);
            fprintf(out, "%u,\"op\":", offset);
            kest_json_text(INSTRUCTIONS[op].name, out);
            fputs(",\"operands\":[", out);
            // How many numbers follow is the width and nothing else, so this
            // cannot come apart from what a walk of the code steps by.
            uint32_t count = (kest_op_width(op) - 1) / 2;
            for (uint32_t k = 0; k < count; k++) {
                fprintf(out, "%s%u", k == 0 ? "" : ",",
                        read_u16(chunk, offset + 1 + k * 2));
            }
            fputs("]}", out);
            offset += kest_op_width(op);
        }
        fputs("]}", out);
    }
    fputc(']', out);
}

void kest_module_disassemble(const KestModule *module,
                             const char *const *entries, FILE *out) {
    // A file of nothing but generic functions has no bodies: a copy exists
    // where one is called, and nothing here called any. A file that declares
    // nothing has none either, and the sentence has to be true of both. It is
    // said whatever else there is to show — a file may lay a shape out and
    // still have nothing with a body in it, which is what `std.sort` became
    // the day its two functions took types.
    if (module->count == 0) {
        fputs("nothing to run: nothing here has a body, and a function that "
              "takes types only gets one where it is called\n",
              out);
    }
    for (uint32_t i = 0; i < module->layout_count; i++) {
        const KestLayout *layout = &module->layouts[i];
        fprintf(out, "layout %u  %u byte%s aligned %u%s:", i, layout->size,
                layout->size == 1 ? "" : "s", layout->align,
                layout->tagged ? ", tagged" : "");
        for (uint16_t p = 0; p < layout->count; p++) {
            fprintf(out, " +%u %s", layout->pieces[p].offset,
                    SCALARS[layout->pieces[p].kind]);
        }
        fputc('\n', out);
    }
    for (uint32_t i = 0; i < module->extern_count; i++) {
        fprintf(out, "host %s\n", module->externs[i].name);
    }

    // What a host has to give the machine before any of this runs. It is the
    // worst of every function, because a host may call anything the program
    // defines, and it is the one number here that is not about the code below
    // it.
    uint32_t stack = 0;
    uint32_t deep = 0;
    KestReason why = {KEST_REACH_UNASKED, NULL};
    // The same byte a function the JSON writer keeps, for the same reason.
    KestNoLeast *reasons =
        module->count == 0
            ? NULL
            : KEST_ARENA_ARRAY(module->arena, KestNoLeast, module->count);
    if (reasons != NULL) {
        memset(reasons, 0, sizeof(KestNoLeast) * module->count);
    }
    if (kest_module_needs(module, module->arena, -1, &stack, &deep, NULL, NULL,
                          reasons, &why)) {
        fprintf(out, "needs %u slot%s and %u frame%s\n", stack,
                stack == 1 ? "" : "s", deep, deep == 1 ? "" : "s");
        // And what an entry point costs on its own, when it is less. A host
        // that calls one of these and nothing else can ask for that instead,
        // and the difference is what the rest of the program costs it. Which
        // names those are is the caller's: this is a library and the names a
        // command line calls are not its business.
        for (uint32_t e = 0; entries != NULL && entries[e] != NULL; e++) {
            int32_t at = kest_module_entry(module, entries[e]);
            uint32_t alone_slots = 0;
            uint32_t alone_deep = 0;
            KestReason alone = {KEST_REACH_UNASKED, NULL};
            if (at >= 0 &&
                kest_module_needs(module, module->arena, at, &alone_slots,
                                  &alone_deep, NULL, NULL, NULL, &alone) &&
                (alone_slots != stack || alone_deep != deep)) {
                fprintf(out, "     %u and %u for `%s` on its own\n",
                        alone_slots, alone_deep, entries[e]);
            }
        }
    } else if (why.reach == KEST_REACH_ITSELF ||
               why.reach == KEST_REACH_VALUE) {
        // The two a host answers by picking a number. The others are not about
        // the program in front of the reader — a build that did not compile
        // has said so already — so nothing is printed for them here.
        fprintf(out, "needs a number a host picks: `%s` %s\n",
                why.where == NULL ? "something here" : why.where,
                kest_reach_name(why.reach));
    }

    for (uint32_t i = 0; i < module->count; i++) {
        const KestChunk *chunk = module->functions[i];
        // What it carries, and not what its declaration says: the machine
        // reads this and nothing else when it checks the one call the second
        // proof cannot see through, and until now nothing anywhere could see
        // it. A generic instance carries what the generic promised, which is
        // a thing worth being able to look at rather than to trust.
        // And which function the walk stopped at, said where a reader is
        // looking when they ask why there is no number. The line above says
        // the program has none and names the function; this is that function,
        // and a reader who came here from the disassembly rather than from the
        // top of it would otherwise have to go back. See D600.
        const char *stopped_at = "";
        const char *came_from = "";
        if (reasons != NULL && reasons[i].reach != 0) {
            stopped_at = kest_reach_name((KestReach)reasons[i].reach);
            // Where it came from, when that is somebody else: the function
            // that is the reason says so by being it.
            if (reasons[i].from != i && reasons[i].from < module->count) {
                came_from = module->functions[reasons[i].from]->name;
            }
        }
        fprintf(out, "fn %s  %u parameter slot%s, %u slot%s, %u deep%s%s%s\n",
                chunk->name, chunk->param_slots,
                chunk->param_slots == 1 ? "" : "s", chunk->slot_count,
                chunk->slot_count == 1 ? "" : "s", chunk->stack_needed,
                chunk->no_alloc ? ", promises `no.alloc`" : "",
                stopped_at[0] == '\0' ? "" : ", ", stopped_at);
        if (came_from[0] != '\0') {
            fprintf(out, "     in %s\n", came_from);
        }
        // And what a machine to call this one takes, which is not the two
        // numbers above it: those are this function's own frame, and this is
        // everything it reaches.
        if (reasons != NULL && reasons[i].reach == 0) {
            fprintf(out, "     %u slot%s and %u frame%s to call it\n",
                    reasons[i].slots, reasons[i].slots == 1 ? "" : "s",
                    reasons[i].frames, reasons[i].frames == 1 ? "" : "s");
        }
        uint32_t offset = 0;
        while (offset < chunk->code_count) {
            offset = disassemble_one(module, chunk, offset, out);
        }
    }
}
