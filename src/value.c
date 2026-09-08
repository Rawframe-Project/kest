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
    size_t length = strlen(name);
    int32_t found = -1;
    for (uint32_t i = 0; i < module->count; i++) {
        const char *candidate = module->functions[i]->name;
        if (strncmp(candidate, name, length) != 0 ||
            candidate[length] != '#') {
            continue;
        }
        if (found >= 0) {
            return -1;
        }
        found = (int32_t)i;
    }
    return found;
}

uint8_t kest_scalar_of(const KestType *type) {
    switch (type->tag) {
    case KEST_T_BOOL:
        return KEST_L_U8;
    case KEST_T_FLOAT:
        return type->width == 32 ? KEST_L_F32 : KEST_L_F64;
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
    if (type->tag == KEST_T_OPTIONAL) {
        at = describe(pieces, at, type->element, base);
        pieces[at].offset = (uint16_t)(base + type->element->byte_size);
        pieces[at].kind = KEST_L_U8;
        return at + 1;
    }
    pieces[at].offset = base;
    pieces[at].kind = kest_scalar_of(type);
    return at + 1;
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
    {"load.at", U16_U16},  {"store.at", U16_U16}, {"len", NONE},
    {"text.len", NONE},    {"text.at", NONE},     {"text.slice", NONE},
    {"text.find", NONE},
    {"text.i", NONE},      {"text.u", NONE},      {"text.f", NONE},
    {"text.f32", NONE},    {"text.b", NONE},
    {"concat", U16},       {"text.from", NONE},
    {"new.store", U16},    {"add", U16},          {"get", U16},
    {"set", U16},          {"remove", NONE},      {"count", NONE},
    {"seek", NONE},        {"store.ref", NONE},
    {"true", NONE},        {"false", NONE},       {"pop", NONE},
    {"pop.n", U16},        {"dup", NONE},         {"rotate", U16},
    {"add.i", NONE},       {"sub.i", NONE},
    {"mul.i", NONE},       {"div.i", NONE},       {"mod.i", NONE},
    {"div.u", NONE},       {"mod.u", NONE},       {"neg.i", NONE},
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
    {"call", U16_U16},     {"call.host", U16_U16_U16},
    {"return", U16},
};

static uint16_t read_u16(const KestChunk *chunk, uint32_t offset) {
    return (uint16_t)(chunk->code[offset] | (chunk->code[offset + 1] << 8));
}

static uint32_t disassemble_one(const KestChunk *chunk, uint32_t offset,
                                FILE *out) {
    uint8_t op = chunk->code[offset];
    const Instruction *instruction = &INSTRUCTIONS[op];
    fprintf(out, "  %04u  %-12s", offset, instruction->name);

    switch (instruction->operands) {
    case NONE:
        fputc('\n', out);
        return offset + 1;
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
        return offset + 3;
    }
    case U16_U16:
        fprintf(out, "%u  %u\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3));
        return offset + 5;
    case U16_U16_U16:
        fprintf(out, "+%u  %u of %u\n", read_u16(chunk, offset + 1),
                read_u16(chunk, offset + 3), read_u16(chunk, offset + 5));
        return offset + 7;
    case JUMP:
        fprintf(out, "%u  -> %u\n", read_u16(chunk, offset + 1),
                offset + 3 + read_u16(chunk, offset + 1));
        return offset + 3;
    case BACK:
        fprintf(out, "%u  -> %u\n", read_u16(chunk, offset + 1),
                offset + 3 - read_u16(chunk, offset + 1));
        return offset + 3;
    }
    return offset + 1;
}

static const char *const SCALARS[] = {"i8",  "i16", "i32", "i64",
                                     "u8",  "u16", "u32", "u64",
                                     "f32", "f64", "word"};

void kest_module_disassemble(const KestModule *module, FILE *out) {
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
