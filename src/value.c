#include "value.h"

#include <string.h>

static void *grow(KestArena *arena, void *items, uint32_t count,
                  uint32_t *capacity, size_t size) {
    uint32_t grown = *capacity == 0 ? 32 : *capacity * 2;
    void *moved = kest_arena_alloc(arena, size * grown, 16);
    if (moved == NULL) {
        return NULL;
    }
    memcpy(moved, items, size * count);
    *capacity = grown;
    return moved;
}

void kest_module_init(KestModule *module, KestArena *arena) {
    module->arena = arena;
    module->functions = NULL;
    module->count = 0;
    module->capacity = 0;
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
    return -1;
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
    JUMP,
    BACK,
    CALL,
} Operands;

typedef struct {
    const char *name;
    Operands operands;
} Instruction;

static const Instruction INSTRUCTIONS[] = {
    {"const", U16},   {"load", U16},    {"store", U16},   {"true", NONE},
    {"false", NONE},  {"pop", NONE},    {"add.i", NONE},  {"sub.i", NONE},
    {"mul.i", NONE},  {"div.i", NONE},  {"mod.i", NONE},  {"div.u", NONE},
    {"mod.u", NONE},  {"neg.i", NONE},  {"add.f", NONE},  {"sub.f", NONE},
    {"mul.f", NONE},  {"div.f", NONE},  {"neg.f", NONE},  {"lt.i", NONE},
    {"le.i", NONE},   {"gt.i", NONE},   {"ge.i", NONE},   {"lt.u", NONE},
    {"le.u", NONE},   {"gt.u", NONE},   {"ge.u", NONE},   {"lt.f", NONE},
    {"le.f", NONE},   {"gt.f", NONE},   {"ge.f", NONE},   {"eq.i", NONE},
    {"ne.i", NONE},   {"eq.f", NONE},   {"ne.f", NONE},   {"eq.t", NONE},
    {"ne.t", NONE},   {"not", NONE},    {"jump", JUMP},   {"jump.false", JUMP},
    {"loop", BACK},   {"call", CALL},   {"print", NONE},  {"return", NONE},
    {"return.void", NONE},
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
    case JUMP:
        fprintf(out, "%u  -> %u\n", read_u16(chunk, offset + 1),
                offset + 3 + read_u16(chunk, offset + 1));
        return offset + 3;
    case BACK:
        fprintf(out, "%u  -> %u\n", read_u16(chunk, offset + 1),
                offset + 3 - read_u16(chunk, offset + 1));
        return offset + 3;
    case CALL:
        fprintf(out, "%u  %u argument%s\n", chunk->code[offset + 1],
                chunk->code[offset + 2],
                chunk->code[offset + 2] == 1 ? "" : "s");
        return offset + 3;
    }
    return offset + 1;
}

void kest_module_disassemble(const KestModule *module, FILE *out) {
    for (uint32_t i = 0; i < module->count; i++) {
        const KestChunk *chunk = module->functions[i];
        fprintf(out, "fn %s  %u parameter%s, %u slot%s\n", chunk->name,
                chunk->param_count, chunk->param_count == 1 ? "" : "s",
                chunk->slot_count, chunk->slot_count == 1 ? "" : "s");
        uint32_t offset = 0;
        while (offset < chunk->code_count) {
            offset = disassemble_one(chunk, offset, out);
        }
    }
}
