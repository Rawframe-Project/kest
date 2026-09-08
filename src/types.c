#include "types.h"

#include <stdlib.h>
#include <string.h>

static void *grow(KestArena *arena, void *items, uint32_t count,
                  uint32_t *capacity, size_t size) {
    uint32_t grown = *capacity == 0 ? 8 : *capacity * 2;
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

static const char *span_string(KestProgram *program, KestSpan span) {
    return kest_arena_strndup(program->arena,
                              program->source->text + span.offset, span.length);
}

// The name a declaration lives under: `world.Npc` for a struct `Npc` in module
// `game.world`, and `Npc` in a file that declares no module.
static const char *qualified(KestProgram *program, KestSpan span) {
    if (program->alias[0] == '\0') {
        return span_string(program, span);
    }
    size_t room = strlen(program->alias) + span.length + 2;
    char *name = kest_arena_alloc(program->arena, room, 1);
    if (name == NULL) {
        return NULL;
    }
    snprintf(name, room, "%s.%.*s", program->alias, (int)span.length,
             program->source->text + span.offset);
    return name;
}

void kest_program_in(KestProgram *program, const KestUnitInfo *unit) {
    program->unit = unit;
    program->source = &unit->source;
    program->alias = unit->alias;
}

// Tries the current file's own module first, then the name as written, which
// is already qualified when it names something imported.
KestType *kest_lookup_type(KestProgram *program, const char *name,
                           size_t length) {
    if (program->alias[0] != '\0') {
        char joined[256];
        int written = snprintf(joined, sizeof(joined), "%s.%.*s",
                               program->alias, (int)length, name);
        if (written > 0 && (size_t)written < sizeof(joined)) {
            KestType *type =
                kest_find_type(program, joined, (size_t)written);
            if (type != NULL) {
                return type;
            }
        }
    }
    return kest_find_type(program, name, length);
}

KestSymbol *kest_lookup_global(KestProgram *program, const char *name,
                               size_t length) {
    if (program->alias[0] != '\0') {
        char joined[256];
        int written = snprintf(joined, sizeof(joined), "%s.%.*s",
                               program->alias, (int)length, name);
        if (written > 0 && (size_t)written < sizeof(joined)) {
            KestSymbol *symbol =
                kest_find_global(program, joined, (size_t)written);
            if (symbol != NULL) {
                return symbol;
            }
        }
    }
    return kest_find_global(program, name, length);
}

static KestType *new_type(KestProgram *program, KestTypeTag tag) {
    KestType *type = KEST_ARENA_NEW(program->arena, KestType);
    if (type != NULL) {
        type->tag = tag;
    }
    return type;
}

static bool register_type(KestProgram *program, KestType *type) {
    if (program->type_count == program->type_capacity) {
        void *moved = grow(program->arena, program->types, program->type_count,
                           &program->type_capacity, sizeof(KestType *));
        if (moved == NULL) {
            return false;
        }
        program->types = moved;
    }
    program->types[program->type_count++] = type;
    return true;
}

bool kest_needs_import(KestProgram *program, const char *name, size_t length) {
    const char *dot = memchr(name, '.', length);
    if (dot == NULL || program->unit == NULL) {
        return false;
    }

    // The question is where the name was declared, not how it is spelled: a
    // host receiver has a dot in it and crosses nothing.
    const KestSource *declared_in = NULL;
    KestType *type = kest_lookup_type(program, name, length);
    if (type != NULL) {
        declared_in = type->declared_in;
    } else {
        KestSymbol *symbol = kest_lookup_global(program, name, length);
        if (symbol != NULL) {
            declared_in = symbol->source;
        }
    }
    if (declared_in == NULL || declared_in == program->source) {
        return false;
    }

    size_t prefix = (size_t)(dot - name);
    if (strlen(program->alias) == prefix &&
        memcmp(program->alias, name, prefix) == 0) {
        return false;
    }
    for (uint32_t i = 0; i < program->unit->import_count; i++) {
        const char *imported = program->unit->imports[i];
        if (strlen(imported) == prefix && memcmp(imported, name, prefix) == 0) {
            return false;
        }
    }
    return true;
}

KestType *kest_find_type(KestProgram *program, const char *name,
                         size_t length) {
    for (uint32_t i = 0; i < program->type_count; i++) {
        const char *candidate = program->types[i]->name;
        // A composed type has no name of its own; `kest_type_name` builds one
        // on demand and nothing looks it up by that.
        if (candidate != NULL && strlen(candidate) == length &&
            memcmp(candidate, name, length) == 0) {
            return program->types[i];
        }
    }
    return NULL;
}

static bool add_primitive(KestProgram *program, const char *name,
                          KestTypeTag tag, uint8_t width, bool is_signed) {
    KestType *type = new_type(program, tag);
    if (type == NULL) {
        return false;
    }
    type->slots = tag == KEST_T_VOID ? 0 : 1;
    // A handle and a piece of text are a machine word. A number is what it
    // says it is.
    type->byte_size = tag == KEST_T_VOID ? 0
                      : width == 0 || width == 1 ? (tag == KEST_T_BOOL ? 1 : 8)
                                                 : (uint16_t)(width / 8);
    type->byte_align = type->byte_size == 0 ? 1 : type->byte_size;
    type->name = name;
    type->width = width;
    type->is_signed = is_signed;
    return register_type(program, type);
}

static bool add_primitives(KestProgram *program) {
    return add_primitive(program, "void", KEST_T_VOID, 0, false) &&
           add_primitive(program, "bool", KEST_T_BOOL, 1, false) &&
           add_primitive(program, "text", KEST_T_TEXT, 0, false) &&
           add_primitive(program, "i8", KEST_T_INT, 8, true) &&
           add_primitive(program, "i16", KEST_T_INT, 16, true) &&
           add_primitive(program, "i32", KEST_T_INT, 32, true) &&
           add_primitive(program, "i64", KEST_T_INT, 64, true) &&
           add_primitive(program, "u8", KEST_T_INT, 8, false) &&
           add_primitive(program, "u16", KEST_T_INT, 16, false) &&
           add_primitive(program, "u32", KEST_T_INT, 32, false) &&
           add_primitive(program, "u64", KEST_T_INT, 64, false) &&
           add_primitive(program, "f32", KEST_T_FLOAT, 32, false) &&
           add_primitive(program, "f64", KEST_T_FLOAT, 64, false);
}

// Levenshtein distance, capped: anything past `limit` is not a suggestion
// worth making, so the walk stops rather than finishing the matrix.
// What a constant of this name is written as. The symbol table first, because
// that is where a name from another file is; then the file being read, because
// a type is resolved before this file's constants are declared and a count may
// name one.
static const KestDecl *constant_in_file(KestProgram *program, const char *name,
                                        uint32_t length) {
    const KestUnit *unit = program->unit == NULL ? NULL : &program->unit->unit;
    for (uint32_t i = 0; unit != NULL && i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind == KEST_DECL_CONST && decl->name.length == length &&
            memcmp(program->source->text + decl->name.offset, name, length) ==
                0) {
            return decl;
        }
    }
    return NULL;
}

static const KestExpr *constant_written(KestProgram *program, const char *name,
                                        uint32_t length) {
    const KestSymbol *symbol = kest_lookup_global(program, name, length);
    if (symbol != NULL && symbol->is_const) {
        return symbol->value;
    }
    const KestDecl *decl = constant_in_file(program, name, length);
    return decl == NULL ? NULL : decl->constant.value;
}

// `f32` rounds where `f64` does not, which is part of what the type means.
static bool is_narrow(const KestType *type) {
    return type != NULL && type->tag == KEST_T_FLOAT && type->width == 32;
}

static bool is_unsigned(const KestType *type) {
    return type != NULL && type->tag == KEST_T_INT && !type->is_signed;
}

// What a constant is, worked out where it is written rather than where it is
// used: one value, so it is the same value everywhere it appears and it costs
// one instruction to push. The checker has already said the expression makes
// sense and what its type is; this only has to do the arithmetic.
//
// False when it is not something that can be worked out here, and then the
// caller says so at the place that asked.
static bool fold(KestProgram *program, const KestExpr *expr, KestValue *out,
                 uint32_t depth, const char **why) {
    if (expr == NULL) {
        return false;
    }
    if (depth > 32) {
        *why = "a constant made out of itself has no value to work out";
        return false;
    }
    const KestType *type = expr->type;
    bool real = type != NULL && type->tag == KEST_T_FLOAT;
    bool unsigned_ = type != NULL && is_unsigned(type);

    switch (expr->kind) {
    case KEST_EXPR_INT: {
        bool overflow = false;
        out->integer = (int64_t)kest_token_integer(
            program->source->text + expr->span.offset, expr->span.length,
            &overflow);
        return true;
    }
    case KEST_EXPR_BYTE: {
        KestSpan content = {expr->span.offset + 1, expr->span.length - 2};
        out->integer =
            (unsigned char)kest_literal_text(program->arena,
                                             program->source, content)[0];
        return true;
    }
    case KEST_EXPR_FLOAT:
        out->real = kest_literal_real(program->source, expr->span);
        if (is_narrow(type)) {
            out->real = (float)out->real;
        }
        return true;
    case KEST_EXPR_STRING: {
        KestSpan content = {expr->span.offset + 1, expr->span.length - 2};
        out->text = kest_literal_text(program->arena, program->source, content);
        return true;
    }
    case KEST_EXPR_BOOL:
        out->integer = expr->boolean;
        return true;
    case KEST_EXPR_TEXT:
        // Filling a hole is what the machine does, and a constant is worked
        // out before there is one.
        *why = "a constant is written without holes in it";
        return false;
    case KEST_EXPR_NAME: {
        // A constant made of itself has no value to work out, which the depth
        // catches; this is only for a name that is not a constant at all.
        const KestExpr *written = constant_written(
            program, program->source->text + expr->span.offset,
            expr->span.length);
        return written != NULL && fold(program, written, out, depth + 1, why);
    }
    case KEST_EXPR_UNARY: {
        KestValue held = {0};
        if (!fold(program, expr->unary.operand, &held, depth + 1, why)) {
            return false;
        }
        switch (expr->unary.op) {
        case KEST_TOK_MINUS:
            if (real) {
                out->real = -held.real;
            } else {
                out->integer = -held.integer;
            }
            return true;
        case KEST_TOK_BANG:
            out->integer = !held.integer;
            return true;
        case KEST_TOK_TILDE:
            out->integer = ~held.integer;
            return true;
        default:
            return false;
        }
    }
    case KEST_EXPR_BINARY: {
        KestValue left = {0};
        KestValue right = {0};
        
        if (!fold(program, expr->binary.left, &left, depth + 1, why) ||
            !fold(program, expr->binary.right, &right, depth + 1, why)) {
            return false;
        }
        // Which arithmetic this is comes from the type the checker settled on
        // for the whole thing, not from the pieces: a comparison of two
        // numbers gives a truth.
        const KestType *side = expr->binary.left->type;
        bool numbers = side != NULL && side->tag == KEST_T_FLOAT;
        if (numbers) {
            double a = left.real;
            double b = right.real;
            switch (expr->binary.op) {
            case KEST_TOK_PLUS:
                out->real = a + b;
                break;
            case KEST_TOK_MINUS:
                out->real = a - b;
                break;
            case KEST_TOK_STAR:
                out->real = a * b;
                break;
            case KEST_TOK_SLASH:
                if (b == 0.0) {
                    *why = "this divides by nought";
                    return false;
                }
                out->real = a / b;
                break;
            case KEST_TOK_LT:
                out->integer = a < b;
                break;
            case KEST_TOK_LTEQ:
                out->integer = a <= b;
                break;
            case KEST_TOK_GT:
                out->integer = a > b;
                break;
            case KEST_TOK_GTEQ:
                out->integer = a >= b;
                break;
            case KEST_TOK_EQEQ:
                out->integer = a == b;
                break;
            case KEST_TOK_BANGEQ:
                out->integer = a != b;
                break;
            default:
                return false;
            }
            if (real && is_narrow(type)) {
                out->real = (float)out->real;
            }
            return true;
        }
        if (side != NULL && side->tag == KEST_T_TEXT) {
            // Text compares and does not add: there is no `+` on text.
            int order = strcmp(left.text, right.text);
            switch (expr->binary.op) {
            case KEST_TOK_EQEQ:
                out->integer = order == 0;
                return true;
            case KEST_TOK_BANGEQ:
                out->integer = order != 0;
                return true;
            case KEST_TOK_LT:
                out->integer = order < 0;
                return true;
            case KEST_TOK_LTEQ:
                out->integer = order <= 0;
                return true;
            case KEST_TOK_GT:
                out->integer = order > 0;
                return true;
            case KEST_TOK_GTEQ:
                out->integer = order >= 0;
                return true;
            default:
                return false;
            }
        }
        int64_t a = left.integer;
        int64_t b = right.integer;
        switch (expr->binary.op) {
        case KEST_TOK_PLUS:
            out->integer = (int64_t)((uint64_t)a + (uint64_t)b);
            break;
        case KEST_TOK_MINUS:
            out->integer = (int64_t)((uint64_t)a - (uint64_t)b);
            break;
        case KEST_TOK_STAR:
            out->integer = (int64_t)((uint64_t)a * (uint64_t)b);
            break;
        case KEST_TOK_SLASH:
            if (b == 0) {
                *why = "this divides by nought";
                return false;
            }
            out->integer = unsigned_ ? (int64_t)((uint64_t)a / (uint64_t)b)
                                     : a / b;
            break;
        case KEST_TOK_PERCENT:
            if (b == 0) {
                *why = "this divides by nought";
                return false;
            }
            out->integer = unsigned_ ? (int64_t)((uint64_t)a % (uint64_t)b)
                                     : a % b;
            break;
        case KEST_TOK_AMP:
            out->integer = a & b;
            break;
        case KEST_TOK_PIPE:
            out->integer = a | b;
            break;
        case KEST_TOK_CARET:
            out->integer = a ^ b;
            break;
        case KEST_TOK_LTLT:
            if (b < 0 || b > 63) {
                return false;
            }
            out->integer = (int64_t)((uint64_t)a << b);
            break;
        case KEST_TOK_GTGT:
            if (b < 0 || b > 63) {
                return false;
            }
            out->integer = unsigned_ ? (int64_t)((uint64_t)a >> b) : a >> b;
            break;
        case KEST_TOK_LT:
            out->integer = unsigned_ ? (uint64_t)a < (uint64_t)b : a < b;
            break;
        case KEST_TOK_LTEQ:
            out->integer = unsigned_ ? (uint64_t)a <= (uint64_t)b : a <= b;
            break;
        case KEST_TOK_GT:
            out->integer = unsigned_ ? (uint64_t)a > (uint64_t)b : a > b;
            break;
        case KEST_TOK_GTEQ:
            out->integer = unsigned_ ? (uint64_t)a >= (uint64_t)b : a >= b;
            break;
        case KEST_TOK_EQEQ:
            out->integer = a == b;
            break;
        case KEST_TOK_BANGEQ:
            out->integer = a != b;
            break;
        case KEST_TOK_AMPAMP:
            out->integer = a && b;
            break;
        case KEST_TOK_PIPEPIPE:
            out->integer = a || b;
            break;
        default:
            return false;
        }
        // A narrower type wraps at its width, the same as it does while
        // running, so a constant and the arithmetic that made it agree.
        if (type != NULL && type->tag == KEST_T_INT && type->width < 64) {
            uint64_t held = (uint64_t)out->integer;
            uint64_t mask = (~(uint64_t)0) >> (64 - type->width);
            held &= mask;
            if (!unsigned_ && (held & (mask ^ (mask >> 1))) != 0) {
                held |= ~mask;
            }
            out->integer = (int64_t)held;
        }
        return true;
    }
    default:
        return false;
    }
}

// A value laid out flat: one slot for a scalar, and a slot per scalar for a
// struct built where it is written. The arithmetic is all scalar, so this is
// only about how many of them there are.
static uint32_t fold_slots(KestProgram *program, const KestExpr *expr,
                           KestValue *out, uint32_t room, uint32_t depth,
                           const char **why) {
    if (expr == NULL || room == 0 || depth > 32) {
        return 0;
    }
    const KestType *type = expr->type;

    if (expr->kind == KEST_EXPR_NAME && type != NULL &&
        (type->tag == KEST_T_STRUCT || type->tag == KEST_T_FIXED)) {
        const KestExpr *written = constant_written(
            program, program->source->text + expr->span.offset,
            expr->span.length);
        if (written != NULL) {
            return fold_slots(program, written, out, room, depth + 1, why);
        }
    }

    // That many of something, written where it stands: the same idea as a
    // struct laid out flat, and the same fold.
    if (type != NULL && type->tag == KEST_T_FIXED &&
        expr->kind == KEST_EXPR_ARRAY) {
        uint32_t used = 0;
        for (uint32_t i = 0; i < expr->array.count; i++) {
            uint32_t wrote = fold_slots(program, expr->array.items[i],
                                        out + used, room - used, depth + 1,
                                        why);
            if (wrote == 0) {
                return 0;
            }
            used += wrote;
        }
        return used == type->slots ? used : 0;
    }

    if (type != NULL && type->tag == KEST_T_STRUCT &&
        expr->kind == KEST_EXPR_CALL) {
        uint32_t used = 0;
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            uint32_t wrote = fold_slots(program, expr->call.args[i], out + used,
                                        room - used, depth + 1, why);
            if (wrote == 0) {
                return 0;
            }
            used += wrote;
        }
        // Every field or none: a struct that was not filled where it was
        // written is not a value yet.
        return used == type->slots ? used : 0;
    }

    // An element of a constant run, or a field of a constant struct, is a
    // constant: worked out here rather than copied into slots and read back.
    if (expr->kind == KEST_EXPR_INDEX || expr->kind == KEST_EXPR_FIELD) {
        const KestExpr *object = expr->kind == KEST_EXPR_INDEX
                                     ? expr->index.object
                                     : expr->field.object;
        const KestType *held = object == NULL ? NULL : object->type;
        uint32_t wide = held == NULL ? 0 : held->slots;
        if (wide == 0 || (held->tag != KEST_T_FIXED &&
                          held->tag != KEST_T_STRUCT)) {
            return 0;
        }
        KestValue *inside =
            KEST_ARENA_ARRAY(program->arena, KestValue, wide);
        if (inside == NULL ||
            fold_slots(program, object, inside, wide, depth + 1, why) != wide) {
            return 0;
        }
        uint32_t from = 0;
        uint32_t many = 0;
        if (expr->kind == KEST_EXPR_INDEX) {
            KestValue where = {0};
            if (held->tag != KEST_T_FIXED ||
                !fold(program, expr->index.index, &where, depth + 1, why) ||
                where.integer < 0 || where.integer >= (int64_t)held->count) {
                return 0;
            }
            many = held->element->slots;
            from = (uint32_t)where.integer * many;
        } else {
            const KestMember *member = NULL;
            for (uint32_t i = 0; i < held->member_count; i++) {
                if (held->members[i].name != NULL &&
                    strlen(held->members[i].name) == expr->field.name.length &&
                    memcmp(held->members[i].name,
                           program->source->text + expr->field.name.offset,
                           expr->field.name.length) == 0) {
                    member = &held->members[i];
                    break;
                }
            }
            if (member == NULL || member->type == NULL) {
                return 0;
            }
            from = member->offset;
            many = member->type->slots;
        }
        if (many == 0 || many > room || from + many > wide) {
            return 0;
        }
        memcpy(out, inside + from, sizeof(KestValue) * many);
        return many;
    }

    KestValue one = {0};
    if (!fold(program, expr, &one, depth, why)) {
        return 0;
    }
    out[0] = one;
    return 1;
}

uint32_t kest_fold_const(KestProgram *program, const KestExpr *expr,
                         KestValue *out, uint32_t room, const char **why) {
    *why = NULL;
    return fold_slots(program, expr, out, room, 0, why);
}

bool kest_type_has_text(const KestType *type, const KestType **without) {
    if (type == NULL) {
        return false;
    }
    switch (type->tag) {
    case KEST_T_ERROR:
    case KEST_T_INT:
    case KEST_T_FLOAT:
    case KEST_T_BOOL:
    case KEST_T_TEXT:
    case KEST_T_FLAGS:
        return true;
    case KEST_T_ENUM:
        for (uint32_t c = 0; c < type->case_count; c++) {
            for (uint32_t p = 0; p < type->cases[c].payload_count; p++) {
                if (!kest_type_has_text(type->cases[c].payload[p], without)) {
                    return false;
                }
            }
        }
        return true;
    // `none`, or what it holds written the way it is written on its own.
    // Both are what a program writes, which is the whole of the rule.
    case KEST_T_OPTIONAL:
        return kest_type_has_text(type->element, without);
    // Written out rather than left to a `default`, so that a tag added to the
    // language does not quietly land on the wrong side of this. The writer in
    // the machine lists the same tags for the same reason, and the two lists
    // are what has to agree.
    case KEST_T_VOID:
    case KEST_T_STRUCT:
    case KEST_T_ARRAY:
    case KEST_T_FIXED:
    case KEST_T_REF:
    case KEST_T_STORE:
    case KEST_T_FN:
    case KEST_T_MODULE:
    case KEST_T_PARAM:
        *without = type;
        return false;
    }
    *without = type;
    return false;
}

const char *kest_type_written(const KestType *type) {
    if (type == NULL || type->name == NULL) {
        return NULL;
    }
    const char *dot = strrchr(type->name, '.');
    return dot == NULL ? type->name : dot + 1;
}

static uint32_t edit_distance(const char *a, size_t a_len, const char *b,
                              size_t b_len, uint32_t limit) {
    if (a_len > b_len + limit || b_len > a_len + limit) {
        return limit + 1;
    }

    uint32_t previous[64];
    uint32_t current[64];
    if (b_len >= 64) {
        return limit + 1;
    }

    for (size_t j = 0; j <= b_len; j++) {
        previous[j] = (uint32_t)j;
    }
    for (size_t i = 1; i <= a_len; i++) {
        current[0] = (uint32_t)i;
        uint32_t best = current[0];
        for (size_t j = 1; j <= b_len; j++) {
            uint32_t substitute = previous[j - 1] + (a[i - 1] != b[j - 1]);
            uint32_t remove = previous[j] + 1;
            uint32_t insert = current[j - 1] + 1;
            uint32_t least = substitute < remove ? substitute : remove;
            current[j] = least < insert ? least : insert;
            if (current[j] < best) {
                best = current[j];
            }
        }
        if (best > limit) {
            return limit + 1;
        }
        memcpy(previous, current, sizeof(uint32_t) * (b_len + 1));
    }
    return previous[b_len];
}

// The closest declared type name, or NULL when nothing is close enough to be
// worth putting in front of a reader.
uint32_t kest_edit_distance(const char *a, size_t a_len, const char *b,
                            size_t b_len, uint32_t limit) {
    return edit_distance(a, a_len, b, b_len, limit);
}

// The closest declared type name, or NULL when nothing is close enough to be
// worth putting in front of a reader. A wrong suggestion costs more than none.
static const char *kest_nearest_type(KestProgram *program, const char *name,
                                     size_t length) {
    // Every one or two character name is one edit from every other, so a
    // suggestion at that length carries no information.
    if (length < 3) {
        return NULL;
    }
    uint32_t limit = length == 3 ? 1 : (uint32_t)length / 3;
    const char *best = NULL;
    uint32_t best_distance = limit + 1;

    for (uint32_t i = 0; i < program->type_count; i++) {
        const char *candidate = program->types[i]->name;
        uint32_t distance =
            edit_distance(name, length, candidate, strlen(candidate), limit);
        if (distance < best_distance) {
            best_distance = distance;
            best = candidate;
        }
    }
    return best;
}

KestType *kest_resolve_type_ref(KestProgram *program,
                                const KestTypeRef *ref);

static KestType *compose(KestProgram *program, KestTypeTag tag,
                         KestType *element) {
    KestType *type = new_type(program, tag);
    if (type == NULL) {
        return NULL;
    }
    type->element = element;
    // A reference and an array are one handle. An optional carries a tag
    // beside whatever it holds, which is what lets a lookup that finds
    // nothing cost no allocation.
    // A reference is one slot: an index with the generation it was handed out
    // at packed above it, so a stale one is recognised rather than followed.
    type->slots = tag == KEST_T_OPTIONAL && element != NULL
                      ? (uint16_t)(element->slots + 1)
                      : 1;
    if (tag == KEST_T_OPTIONAL && element != NULL) {
        // What it holds, then a byte saying whether it does, laid out the way
        // a C struct of the two would be.
        type->byte_align = element->byte_align;
        uint16_t used = (uint16_t)(element->byte_size + 1);
        uint16_t align = type->byte_align == 0 ? 1 : type->byte_align;
        type->byte_size = (uint16_t)((used + align - 1) / align * align);
    } else {
        type->byte_size = 8;
        type->byte_align = 8;
    }
    return type;
}

static KestType *error_type(KestProgram *program) {
    return new_type(program, KEST_T_ERROR);
}

static KestType *resolve_named(KestProgram *program, const KestTypeRef *ref) {
    const char *name = program->source->text + ref->name.offset;
    size_t length = ref->name.length;

    // A type name a generic function brought into scope stands for whatever
    // this instance was given, or for itself while the signature is declared.
    for (uint32_t i = 0; i < program->bound_count; i++) {
        if (strlen(program->bound_names[i]) == length &&
            memcmp(program->bound_names[i], name, length) == 0) {
            return program->bound_types[i];
        }
    }

    KestType *type = kest_lookup_type(program, name, length);
    if (type != NULL && type->type_param_count > 0) {
        kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0302", ref->name,
                       "`%s` takes %u type%s, and none are written here",
                       type->name, type->type_param_count,
                       type->type_param_count == 1 ? "" : "s");
        kest_diags_suggest(program->diags, "write them: `%s<i32>`", type->name);
        return error_type(program);
    }
    if (type != NULL) {
        if (kest_needs_import(program, name, length)) {
            const char *dot = memchr(name, '.', length);
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0325",
                           ref->name, "this file does not import `%.*s`",
                           (int)(dot - name), name);
            kest_diags_suggest(program->diags,
                               "a name is only reachable from a module this "
                               "file asked for");
            if (type->declared_in != NULL) {
                kest_diags_note(program->diags, type->declared_in, type->span,
                                "declared here");
            }
        }
        return type;
    }

    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0301", ref->name,
                   "unknown type `%.*s`", (int)length, name);
    const char *nearest = kest_nearest_type(program, name, length);
    if (nearest != NULL) {
        kest_diags_suggest(program->diags, "did you mean `%s`?", nearest);
    }
    return error_type(program);
}

KestType *kest_array_of(KestProgram *program, KestType *element) {
    return compose(program, KEST_T_ARRAY, element);
}

KestType *kest_optional_of(KestProgram *program, KestType *element) {
    return compose(program, KEST_T_OPTIONAL, element);
}

KestType *kest_ref_of(KestProgram *program, KestType *element) {
    return compose(program, KEST_T_REF, element);
}

// That many of something, laid out where it stands. Unlike an array it is a
// value: copying one copies all of it, and a struct holding one holds the
// whole thing rather than a handle to it.
KestType *kest_fixed_of(KestProgram *program, KestType *element,
                        uint32_t count) {
    // Composed like an array or an optional, and like them not registered:
    // it has no name to be found under and two of them are one type by what
    // they hold rather than by being the same one.
    KestType *type = new_type(program, KEST_T_FIXED);
    if (type == NULL) {
        return error_type(program);
    }
    type->element = element;
    type->count = count;
    if (element != NULL) {
        type->slots = (uint16_t)(element->slots * count);
        type->byte_size = (uint16_t)(element->byte_size * count);
        type->byte_align = element->byte_align;
    }
    return type;
}

// A function as a value. One slot holding which function it is, and what it
// promises is part of what it is: a value that promises `no.alloc` may go
// where one that does not is wanted, and not the other way round, which is
// what keeps a cost contract provable through an indirect call.
// A function as a value. What it promises is part of what it is.
static KestType *kest_fn_of(KestProgram *program, KestType **params,
                            uint32_t count,
                     KestType *result, bool no_alloc) {
    KestType *type = new_type(program, KEST_T_FN);
    if (type == NULL) {
        return NULL;
    }
    type->params = KEST_ARENA_ARRAY(program->arena, KestType *,
                                    count == 0 ? 1 : count);
    if (type->params == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < count; i++) {
        type->params[i] = params[i];
    }
    type->param_count = count;
    type->result = result;
    type->no_alloc = no_alloc;
    type->slots = 1;
    type->byte_size = 8;
    type->byte_align = 8;
    return type;
}

static bool measure(KestProgram *program, KestType *type);
static bool sized_within(KestProgram *program, uint32_t bytes, uint32_t slots,
                         const KestSource *where, KestSpan span,
                         const char *what);

// One copy of a generic struct per set of types. The copy is a struct like any
// other by the time anything else sees it: fields resolved, laid out, and
// measured, so nothing downstream knows it came from a shape.
KestType *kest_struct_of(KestProgram *program, KestType *shape, KestType **args,
                         uint32_t count) {
    char written[256];
    size_t used = (size_t)snprintf(written, sizeof(written), "%s<", shape->name);
    for (uint32_t i = 0; i < count && used < sizeof(written); i++) {
        used += (size_t)snprintf(written + used, sizeof(written) - used, "%s%s",
                                 i == 0 ? "" : ", ",
                                 kest_type_name(program->arena, args[i]));
    }
    if (used < sizeof(written)) {
        snprintf(written + used, sizeof(written) - used, ">");
    }
    KestType *made = kest_find_type(program, written, strlen(written));
    if (made != NULL) {
        return made;
    }

    const char *name = kest_arena_strndup(program->arena, written,
                                          strlen(written));
    made = new_type(program, KEST_T_STRUCT);
    if (name == NULL || made == NULL || !register_type(program, made)) {
        return error_type(program);
    }
    made->name = name;
    made->span = shape->span;
    made->declared_in = shape->declared_in;
    // Which shape this is a copy of, so a value built by naming the shape can
    // be recognised as this one.
    made->decl = shape->decl;
    made->unit = shape->unit;
    made->shape = shape;
    made->type_args = KEST_ARENA_ARRAY(program->arena, KestType *,
                                       count == 0 ? 1 : count);
    if (made->type_args == NULL) {
        return error_type(program);
    }
    for (uint32_t i = 0; i < count; i++) {
        made->type_args[i] = args[i];
    }
    made->type_arg_count = count;

    const KestDecl *decl = shape->decl;
    const KestUnitInfo *was_unit = program->unit;
    const KestSource *was_source = program->source;
    const char *was_alias = program->alias;
    kest_program_in(program, (KestUnitInfo *)shape->unit);

    const char *names[8];
    KestType *bound[8];
    for (uint32_t i = 0; i < count; i++) {
        names[i] = shape->type_param_names[i];
        bound[i] = args[i];
    }
    // A copy may name the shape again with other types, so what was bound
    // before this one has to come back after it.
    const char *was_names[8];
    KestType *was_types[8];
    uint32_t was_count = program->bound_count;
    for (uint32_t i = 0; i < was_count; i++) {
        was_names[i] = program->bound_names[i];
        was_types[i] = program->bound_types[i];
    }
    kest_bind_types(program, names, bound, count);

    uint32_t fields = decl->record.field_count;
    KestMember *members =
        KEST_ARENA_ARRAY(program->arena, KestMember, fields == 0 ? 1 : fields);
    if (members == NULL) {
        return error_type(program);
    }
    for (uint32_t f = 0; f < fields; f++) {
        members[f].name = span_string(program, decl->record.fields[f]->name);
        members[f].span = decl->record.fields[f]->name;
        members[f].type =
            kest_resolve_type_ref(program, decl->record.fields[f]->type);
    }
    made->members = members;
    made->member_count = fields;

    kest_bind_types(program, was_names, was_types, was_count);
    program->unit = was_unit;
    program->source = was_source;
    program->alias = was_alias;

    measure(program, made);
    return made;
}

KestType *kest_resolve_type_ref(KestProgram *program,
                                const KestTypeRef *ref) {
    if (ref == NULL) {
        return error_type(program);
    }

    switch (ref->kind) {
    case KEST_TYPE_FN: {
        KestType *params[16];
        uint32_t count = ref->arg_count < 16 ? ref->arg_count : 16;
        for (uint32_t i = 0; i < count; i++) {
            params[i] = kest_resolve_type_ref(program, ref->args[i]);
        }
        KestType *result = ref->element == NULL
                               ? kest_lookup_type(program, "void", 4)
                               : kest_resolve_type_ref(program, ref->element);
        return kest_fn_of(program, params, count, result, ref->no_alloc);
    }

    case KEST_TYPE_NAMED:
        return resolve_named(program, ref);

    case KEST_TYPE_GENERIC: {
        const char *name = program->source->text + ref->name.offset;
        bool is_ref = ref->name.length == 3 && memcmp(name, "ref", 3) == 0;
        bool is_store = ref->name.length == 5 && memcmp(name, "store", 5) == 0;
        if (!is_ref && !is_store) {
            // `Pair<i32, text>`: a copy of a shape, made the first time it is
            // written and found again after that.
            KestType *shape = kest_lookup_type(program, name, ref->name.length);
            if (shape != NULL && shape->type_param_count > 0) {
                KestType *args[8];
                uint32_t count = ref->arg_count < 8 ? ref->arg_count : 8;
                for (uint32_t i = 0; i < count; i++) {
                    args[i] = kest_resolve_type_ref(program, ref->args[i]);
                }
                if (ref->arg_count != shape->type_param_count) {
                    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0302",
                                   ref->span,
                                   "`%s` takes %u type%s, found %u",
                                   shape->name, shape->type_param_count,
                                   shape->type_param_count == 1 ? "" : "s",
                                   ref->arg_count);
                    return error_type(program);
                }
                return kest_struct_of(program, shape, args, count);
            }
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0302",
                           ref->name, "unknown generic type `%.*s`",
                           (int)ref->name.length, name);
            kest_diags_suggest(program->diags,
                               "`ref<T>` and `store<T>` are the two");
            return error_type(program);
        }
        if (ref->arg_count != 1) {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0302",
                           ref->span, "`%s` takes one type argument, found %u",
                           is_ref ? "ref" : "store", ref->arg_count);
            return error_type(program);
        }
        return compose(program, is_ref ? KEST_T_REF : KEST_T_STORE,
                       kest_resolve_type_ref(program, ref->args[0]));
    }

    case KEST_TYPE_ARRAY: {
        KestType *element = kest_resolve_type_ref(program, ref->element);
        if (ref->count.length == 0) {
            return compose(program, KEST_T_ARRAY, element);
        }
        // A number, or the name of a constant that is one. D064 asked for a
        // literal because a name could be a size that changes; a constant is
        // worked out where it is written and cannot, and a program with the
        // same number in five places is the thing that changes wrongly.
        const char *digits = program->source->text + ref->count.offset;
        uint64_t how_many = 0;
        if (digits[0] >= '0' && digits[0] <= '9') {
            for (uint32_t i = 0; i < ref->count.length; i++) {
                how_many = how_many * 10 + (uint64_t)(digits[i] - '0');
            }
        } else {
            // Looked up in the file being read rather than in the symbol
            // table, because a type is resolved before the constants are
            // declared: a struct's fields are what a constant of that struct
            // is measured from, so constants cannot come first.
            // A count names a constant in this file: the token is one name
            // and a name from another file has a dot in it. Its declared type
            // is what says it is a number, because nothing has been checked
            // yet when a type is being resolved.
            const KestDecl *declared =
                constant_in_file(program, digits, ref->count.length);
            const KestType *counted =
                declared == NULL
                    ? NULL
                    : kest_resolve_type_ref(program, declared->constant.type);
            KestValue value = {0};
            const char *why = NULL;
            if (declared == NULL || counted == NULL ||
                counted->tag != KEST_T_INT) {
                kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0326",
                               ref->count,
                               "a count is a number or a constant that is one");
                kest_diags_suggest(program->diags,
                                   "`const N: i32 = 16` and then `[T; N]`");
                return error_type(program);
            }
            if (kest_fold_const(program, declared->constant.value, &value, 1,
                                &why) != 1) {
                kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0326",
                               ref->count,
                               "this count is not worked out where it is "
                               "written");
                kest_diags_suggest(program->diags, "%s",
                                   why != NULL ? why
                                               : "a constant is a number, a "
                                                 "truth or a piece of text, "
                                                 "and arithmetic on those");
                return error_type(program);
            }
            how_many = value.integer < 0 ? 0 : (uint64_t)value.integer;
        }
        if (how_many == 0 || how_many > 65535) {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0326",
                           ref->count,
                           "an array of that many has no size: %llu",
                           (unsigned long long)how_many);
            kest_diags_suggest(program->diags,
                               "between one and 65535, and `[T]` for one that "
                               "grows");
            return error_type(program);
        }
        // What it holds may not be measured yet — a struct is measured after
        // the fields that name it are resolved — and then this is sized again
        // where it is held. Where it is already known, it is known here.
        KestType *run = kest_fixed_of(program, element, (uint32_t)how_many);
        if (element != NULL && element->byte_size != 0 &&
            !sized_within(program, (uint32_t)element->byte_size * how_many,
                          (uint32_t)element->slots * how_many, program->source,
                          ref->count, kest_type_name(program->arena, run))) {
            return error_type(program);
        }
        return run;
    }

    case KEST_TYPE_OPTIONAL:
        return compose(program, KEST_T_OPTIONAL,
                       kest_resolve_type_ref(program, ref->element));
    }
    return error_type(program);
}

const char *kest_type_name(KestArena *arena, const KestType *type) {
    if (type == NULL) {
        return "?";
    }
    if (type->name != NULL) {
        return type->name;
    }

    if (type->tag == KEST_T_FN) {
        char written[256];
        size_t used = (size_t)snprintf(written, sizeof(written), "fn(");
        for (uint32_t i = 0; i < type->param_count && used < sizeof(written);
             i++) {
            used += (size_t)snprintf(written + used, sizeof(written) - used,
                                     "%s%s", i == 0 ? "" : ", ",
                                     kest_type_name(arena, type->params[i]));
        }
        if (used < sizeof(written)) {
            used += (size_t)snprintf(written + used, sizeof(written) - used,
                                     ")");
        }
        if (type->result != NULL && type->result->tag != KEST_T_VOID &&
            used < sizeof(written)) {
            used += (size_t)snprintf(written + used, sizeof(written) - used,
                                     " -> %s",
                                     kest_type_name(arena, type->result));
        }
        if (type->no_alloc && used < sizeof(written)) {
            snprintf(written + used, sizeof(written) - used, " no.alloc");
        }
        return kest_arena_strndup(arena, written, strlen(written));
    }

    const char *inner = kest_type_name(arena, type->element);
    char buffer[256];
    switch (type->tag) {
    case KEST_T_ARRAY:
        snprintf(buffer, sizeof(buffer), "[%s]", inner);
        break;
    case KEST_T_FIXED:
        snprintf(buffer, sizeof(buffer), "[%s; %u]", inner, type->count);
        break;
    case KEST_T_REF:
        snprintf(buffer, sizeof(buffer), "ref<%s>", inner);
        break;
    case KEST_T_STORE:
        snprintf(buffer, sizeof(buffer), "store<%s>", inner);
        break;
    case KEST_T_OPTIONAL:
        snprintf(buffer, sizeof(buffer), "%s?", inner);
        break;
    case KEST_T_ERROR:
        return "<unknown>";
    default:
        return "?";
    }
    return kest_arena_strndup(arena, buffer, strlen(buffer));
}

uint32_t kest_overloads(KestProgram *program, const char *name, size_t length,
                        KestSymbol **found, uint32_t room) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < program->global_count && count < room; i++) {
        const char *candidate = program->globals[i].name;
        if (strlen(candidate) == length &&
            memcmp(candidate, name, length) == 0 &&
            program->globals[i].type->tag == KEST_T_FN) {
            found[count++] = &program->globals[i];
        }
    }
    return count;
}

KestSymbol *kest_symbol_at(KestProgram *program, const KestSource *source,
                           KestSpan span) {
    for (uint32_t i = 0; i < program->global_count; i++) {
        if (program->globals[i].source == source &&
            program->globals[i].span.offset == span.offset) {
            return &program->globals[i];
        }
    }
    return NULL;
}

KestSymbol *kest_find_global(KestProgram *program, const char *name,
                             size_t length) {
    for (uint32_t i = 0; i < program->global_count; i++) {
        const char *candidate = program->globals[i].name;
        if (strlen(candidate) == length &&
            memcmp(candidate, name, length) == 0) {
            return &program->globals[i];
        }
    }
    return NULL;
}

// What a function is compiled under: its name and what it takes. Two
// functions sharing a name are two functions and need two of these.
static const char *symbol_of(KestProgram *program, const char *name,
                             const KestType *type) {
    char buffer[512];
    int used = snprintf(buffer, sizeof(buffer), "%s", name);
    for (uint32_t i = 0; i < type->param_count && used > 0 &&
                         (size_t)used < sizeof(buffer);
         i++) {
        used += snprintf(buffer + used, sizeof(buffer) - (size_t)used, "%c%s",
                         i == 0 ? '#' : ',',
                         kest_type_name(program->arena, type->params[i]));
    }
    if (used <= 0 || (size_t)used >= sizeof(buffer)) {
        return name;
    }
    return kest_arena_strndup(program->arena, buffer, (size_t)used);
}

// Whether these two take exactly the same things, which is the only way two
// functions of one name are the same function.
static bool same_parameters(const KestType *a, const KestType *b) {
    if (a->param_count != b->param_count) {
        return false;
    }
    for (uint32_t i = 0; i < a->param_count; i++) {
        if (!kest_type_equal(a->params[i], b->params[i])) {
            return false;
        }
    }
    return true;
}

static bool add_global_value(KestProgram *program, const char *name,
                             KestType *type, KestSpan span, bool is_const,
                             const KestExpr *value);

static bool add_global(KestProgram *program, const char *name, KestType *type,
                       KestSpan span, bool is_const) {
    return add_global_value(program, name, type, span, is_const, NULL);
}

static bool add_global_value(KestProgram *program, const char *name,
                             KestType *type, KestSpan span, bool is_const,
                             const KestExpr *value) {
    KestSymbol *existing = kest_find_global(program, name, strlen(name));
    // Two functions may share a name when they take different things. Two of
    // anything else may not, and neither may two that take the same things.
    if (existing != NULL && type->tag == KEST_T_FN &&
        existing->type->tag == KEST_T_FN && !type->is_foreign &&
        !existing->type->is_foreign) {
        uint32_t count = 0;
        KestSymbol *all[32];
        count = kest_overloads(program, name, strlen(name), all, 32);
        existing = NULL;
        for (uint32_t i = 0; i < count; i++) {
            if (same_parameters(all[i]->type, type)) {
                existing = all[i];
                break;
            }
        }
    }
    if (existing != NULL) {
        kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0304", span,
                       "`%s` is already declared", name);
        kest_diags_note(program->diags, existing->source, existing->span,
                        "the first one");
        return true;
    }

    if (program->global_count == program->global_capacity) {
        void *moved =
            grow(program->arena, program->globals, program->global_count,
                 &program->global_capacity, sizeof(KestSymbol));
        if (moved == NULL) {
            return false;
        }
        program->globals = moved;
    }

    KestSymbol *symbol = &program->globals[program->global_count++];
    symbol->name = name;
    symbol->type = type;
    symbol->span = span;
    symbol->source = program->source;
    symbol->is_const = is_const;
    symbol->value = value;
    return true;
}

// Structs are registered before any field is resolved, so two of them may name
// each other and a field may name the type it belongs to.
static bool declare_structs(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_STRUCT) {
            continue;
        }
        const char *name = qualified(program, decl->name);
        if (name == NULL) {
            return false;
        }
        KestType *existing = kest_find_type(program, name, strlen(name));
        if (existing != NULL) {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0304",
                           decl->name, "`%s` is already declared", name);
            kest_diags_note(program->diags, existing->declared_in,
                            existing->span, "the first one");
            continue;
        }
        KestType *type = new_type(program, KEST_T_STRUCT);
        if (type == NULL || !register_type(program, type)) {
            return false;
        }
        type->name = name;
        type->span = decl->name;
        type->declared_in = program->source;

        // A generic struct is not a type but the shape of one. `Pair<i32>` is
        // a type; `Pair` on its own has no size and is never measured.
        if (decl->type_param_count > 0) {
            type->type_param_count = decl->type_param_count;
            type->type_param_names = KEST_ARENA_ARRAY(
                program->arena, const char *, decl->type_param_count);
            if (type->type_param_names == NULL) {
                return false;
            }
            for (uint32_t g = 0; g < decl->type_param_count; g++) {
                type->type_param_names[g] =
                    span_string(program, decl->type_params[g]);
                if (type->type_param_names[g] == NULL) {
                    return false;
                }
            }
            type->decl = decl;
            type->unit = program->unit;
        }
    }
    return true;
}

static bool resolve_struct_fields(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_STRUCT) {
            continue;
        }
        const char *name = qualified(program, decl->name);
        KestType *type = kest_find_type(program, name, strlen(name));
        if (type == NULL || type->members != NULL) {
            continue;
        }
        // A shape's fields are resolved with its names standing for
        // themselves, so a use can put what it was given beside them and see
        // what each one has to be. It is never measured; a copy is.
        const char *names[8];
        KestType *stands[8];
        uint32_t generics = type->type_param_count > 8 ? 8
                                                       : type->type_param_count;
        for (uint32_t g = 0; g < generics; g++) {
            names[g] = type->type_param_names[g];
            stands[g] = new_type(program, KEST_T_PARAM);
            if (stands[g] == NULL) {
                return false;
            }
            stands[g]->name = names[g];
            stands[g]->slots = 1;
            stands[g]->byte_size = 8;
            stands[g]->byte_align = 8;
        }
        kest_bind_types(program, names, stands, generics);

        uint32_t count = decl->record.field_count;
        KestMember *members = KEST_ARENA_ARRAY(program->arena, KestMember,
                                               count == 0 ? 1 : count);
        if (members == NULL) {
            return false;
        }

        uint32_t used = 0;
        for (uint32_t f = 0; f < count; f++) {
            const KestField *field = decl->record.fields[f];
            const char *field_name = span_string(program, field->name);
            if (field_name == NULL) {
                return false;
            }

            bool duplicate = false;
            for (uint32_t seen = 0; seen < used; seen++) {
                if (strcmp(members[seen].name, field_name) == 0) {
                    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0303",
                                   field->name,
                                   "field `%s` is declared twice in `%s`",
                                   field_name, name);
                    kest_diags_note(program->diags, NULL, members[seen].span,
                                    "the first one");
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }

            members[used].name = field_name;
            members[used].type = kest_resolve_type_ref(program, field->type);
            members[used].span = field->name;
            used++;
        }

        type->members = members;
        type->member_count = used;
        kest_unbind_types(program);
    }
    return true;
}

// A value's size is what it holds, so a thing that holds itself has none.
// Structs and enums are measured together because either may hold the other,
// and both are broken by a `ref`, which is one word whatever it points at.
static const char *span_string(KestProgram *program, KestSpan span);
static bool register_type(KestProgram *program, KestType *type);
static KestType *new_type(KestProgram *program, KestTypeTag tag);
static KestType *error_type(KestProgram *program);

// A value is at most 65535 bytes and as many slots, because that is what a
// layout says one is and a value is a frame's worth rather than a heap's. The
// sentence is here and the place it is said about is the caller's, which is
// either where the count was written or the struct that came out too big.
static bool sized_within(KestProgram *program, uint32_t bytes, uint32_t slots,
                         const KestSource *where, KestSpan span,
                         const char *what) {
    if (bytes <= UINT16_MAX && slots <= UINT16_MAX) {
        return true;
    }
    kest_diags_in(program->diags, where);
    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0327", span,
                   "`%s` is %u bytes, and a value is at most %u", what, bytes,
                   UINT16_MAX);
    kest_diags_suggest(program->diags,
                       "`[T]` holds that many on the heap and is a handle");
    return false;
}

static bool measure_held(KestProgram *program, KestType *type,
                         const KestType *whole) {
    if (type == NULL) {
        return true;
    }
    // That many of something is sized from what it holds, and it is composed
    // while fields are resolved — before the structs among them are measured.
    // So it is sized here, where what it holds has just been.
    if (type->tag == KEST_T_FIXED) {
        if (!measure_held(program, type->element, whole)) {
            return false;
        }
        const KestType *element = type->element;
        uint32_t one = element == NULL || element->slots == 0 ? 1
                                                              : element->slots;
        uint32_t bytes = element == NULL || element->byte_size == 0
                             ? 8
                             : element->byte_size;
        type->byte_align = element == NULL || element->byte_align == 0
                               ? 8
                               : element->byte_align;
        if (!sized_within(program, bytes * type->count, one * type->count,
                          whole == NULL ? NULL : whole->declared_in,
                          whole == NULL ? type->span : whole->span,
                          kest_type_name(program->arena, type))) {
            type->slots = 1;
            type->byte_size = 8;
            type->byte_align = 8;
            return true;
        }
        type->slots = (uint16_t)(one * type->count);
        type->byte_size = (uint16_t)(bytes * type->count);
        return true;
    }
    if (type->tag != KEST_T_STRUCT && type->tag != KEST_T_ENUM) {
        return true;
    }
    return measure(program, type);
}

static bool refuse_cycle(KestProgram *program, KestType *type) {
    kest_diags_in(program->diags, type->declared_in);
    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0319", type->span,
                   "`%s` contains itself, so it has no size", type->name);
    kest_diags_suggest(program->diags,
                       "hold it through `ref<%s>`, which is a handle",
                       type->name);
    // One word, so the rest of the file is still checkable against a type
    // that has a size even though it is the wrong one.
    type->slots = 1;
    type->byte_size = 8;
    type->byte_align = 8;
    type->sizing = false;
    return false;
}

static bool measure_struct(KestProgram *program, KestType *type) {
    uint16_t offset = 0;
    uint16_t bytes = 0;
    uint16_t align = 1;

    for (uint32_t i = 0; i < type->member_count; i++) {
        KestType *member = type->members[i].type;
        if (!measure_held(program, member, type)) {
            return refuse_cycle(program, type);
        }
        type->members[i].offset = offset;
        offset += member == NULL ? 1 : member->slots;

        // The bytes are laid out the way a C compiler would, so an array of
        // these can be the array the host already has.
        uint16_t member_size = member == NULL ? 8 : member->byte_size;
        uint16_t member_align = member == NULL || member->byte_align == 0
                                    ? 8
                                    : member->byte_align;
        bytes = (uint16_t)((bytes + member_align - 1) / member_align *
                           member_align);
        type->members[i].byte_offset = bytes;
        bytes += member_size;
        if (member_align > align) {
            align = member_align;
        }
    }

    type->slots = offset == 0 ? 1 : offset;
    type->byte_align = align;
    type->byte_size =
        bytes == 0 ? 1 : (uint16_t)((bytes + align - 1) / align * align);
    return true;
}

// An enum is a tag and whichever case's payload is widest, which is what a
// tagged union is and why every case can be read for its tag alone.
static bool measure_enum(KestProgram *program, KestType *type) {
    uint16_t payload_slots = 0;
    uint16_t payload_bytes = 0;
    uint16_t align = 4;

    for (uint32_t c = 0; c < type->case_count; c++) {
        KestVariantType *variant = &type->cases[c];
        uint16_t slots = 0;
        uint16_t bytes = 0;
        for (uint32_t p = 0; p < variant->payload_count; p++) {
            KestType *held = variant->payload[p];
            if (!measure_held(program, held, type)) {
                return refuse_cycle(program, type);
            }
            uint16_t held_align = held == NULL || held->byte_align == 0
                                      ? 8
                                      : held->byte_align;
            if (held_align > align) {
                align = held_align;
            }
            variant->offsets[p] = slots;
            slots += held == NULL ? 1 : held->slots;
            bytes = (uint16_t)((bytes + held_align - 1) / held_align *
                               held_align);
            variant->byte_offsets[p] = bytes;
            bytes += held == NULL ? 8 : held->byte_size;
        }
        if (slots > payload_slots) {
            payload_slots = slots;
        }
        if (bytes > payload_bytes) {
            payload_bytes = bytes;
        }
    }

    // The tag is a four byte integer, so the payload starts wherever its own
    // alignment puts it after that.
    uint16_t start = (uint16_t)((4 + align - 1) / align * align);
    for (uint32_t c = 0; c < type->case_count; c++) {
        for (uint32_t p = 0; p < type->cases[c].payload_count; p++) {
            type->cases[c].offsets[p] =
                (uint16_t)(type->cases[c].offsets[p] + 1);
            type->cases[c].byte_offsets[p] =
                (uint16_t)(type->cases[c].byte_offsets[p] + start);
        }
    }

    type->slots = (uint16_t)(payload_slots + 1);
    type->byte_align = align;
    uint16_t total = (uint16_t)(start + payload_bytes);
    type->byte_size = (uint16_t)((total + align - 1) / align * align);
    return true;
}

static bool measure(KestProgram *program, KestType *type) {
    if (type->slots > 0) {
        return true;
    }
    if (type->sizing) {
        return false;
    }
    type->sizing = true;
    bool ok = type->tag == KEST_T_ENUM ? measure_enum(program, type)
                                       : measure_struct(program, type);
    type->sizing = false;
    return ok;
}

static bool measure_all(KestProgram *program) {
    for (uint32_t i = 0; i < program->type_count; i++) {
        KestType *type = program->types[i];
        if (type->type_param_count > 0) {
            continue;
        }
        if (type->tag == KEST_T_STRUCT || type->tag == KEST_T_ENUM) {
            const KestSource *was = program->source;
            measure(program, type);
            kest_diags_in(program->diags, was);
        }
    }
    return true;
}

static bool declare_enums(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_ENUM) {
            continue;
        }
        const char *name = qualified(program, decl->name);
        if (name == NULL) {
            return false;
        }
        KestType *existing = kest_find_type(program, name, strlen(name));
        if (existing != NULL) {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0304",
                           decl->name, "`%s` is already declared", name);
            kest_diags_note(program->diags, existing->declared_in,
                            existing->span, "the first one");
            continue;
        }
        KestType *type = new_type(program, KEST_T_ENUM);
        if (type == NULL || !register_type(program, type)) {
            return false;
        }
        type->name = name;
        type->span = decl->name;
        type->declared_in = program->source;
    }
    return true;
}

// A set of named bits. The names are the cases, in order, and the bit a case
// stands for is its position: nothing is written down that could be counted,
// and there are no powers of two to get wrong.
static bool declare_flags(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_FLAGS) {
            continue;
        }
        const char *name = qualified(program, decl->name);
        if (name == NULL) {
            return false;
        }
        KestType *existing = kest_find_type(program, name, strlen(name));
        if (existing != NULL) {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0304",
                           decl->name, "`%s` is already declared", name);
            kest_diags_note(program->diags, existing->declared_in,
                            existing->span, "the first one");
            continue;
        }
        KestType *type = new_type(program, KEST_T_FLAGS);
        if (type == NULL || !register_type(program, type)) {
            return false;
        }
        type->name = name;
        type->span = decl->name;
        type->declared_in = program->source;
    }
    return true;
}

static bool resolve_flags_cases(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_FLAGS) {
            continue;
        }
        const char *name = qualified(program, decl->name);
        KestType *type = kest_find_type(program, name, strlen(name));
        if (type == NULL || type->cases != NULL) {
            continue;
        }

        KestType *over = kest_resolve_type_ref(program, decl->choice.width);
        if (over == NULL || over->tag == KEST_T_ERROR) {
            continue;
        }
        if (over->tag != KEST_T_INT || over->is_signed) {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0337",
                           decl->choice.width->span,
                           "flags sit over an unsigned integer, found `%s`",
                           kest_type_name(program->arena, over));
            kest_diags_suggest(program->diags,
                               "`u8`, `u16`, `u32` or `u64` says how many "
                               "bits the host sees");
            continue;
        }
        // One slot however wide it is declared, because every value of every
        // width fits in one. The byte layout is the declared width, which is
        // what a host reading the same memory sees (D016).
        type->width = over->width;
        type->is_signed = false;
        type->slots = 1;
        type->byte_size = (uint16_t)(over->width / 8);
        type->byte_align = type->byte_size;

        uint32_t count = decl->choice.case_count;
        KestVariantType *cases = KEST_ARENA_ARRAY(program->arena,
                                                  KestVariantType,
                                                  count == 0 ? 1 : count);
        if (cases == NULL) {
            return false;
        }

        uint32_t used = 0;
        for (uint32_t c = 0; c < count; c++) {
            const KestVariant *written = decl->choice.cases[c];
            const char *case_name = span_string(program, written->name);
            if (case_name == NULL) {
                return false;
            }
            bool duplicate = false;
            for (uint32_t seen = 0; seen < used; seen++) {
                if (strcmp(cases[seen].name, case_name) == 0) {
                    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0303",
                                   written->name,
                                   "flag `%s` is declared twice in `%s`",
                                   case_name, name);
                    kest_diags_note(program->diags, NULL, cases[seen].span,
                                    "the first one");
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
            if (used >= over->width) {
                kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0338",
                               written->name,
                               "`%s` is flag %u, and a `%s` holds %u",
                               case_name, used + 1,
                               kest_type_name(program->arena, over),
                               over->width);
                kest_diags_suggest(program->diags,
                                   "widen the type the flags sit over");
                continue;
            }
            cases[used].name = case_name;
            cases[used].span = written->name;
            cases[used].payload_count = 0;
            cases[used].payload =
                KEST_ARENA_ARRAY(program->arena, KestType *, 1);
            cases[used].offsets = KEST_ARENA_ARRAY(program->arena, uint16_t, 1);
            cases[used].byte_offsets =
                KEST_ARENA_ARRAY(program->arena, uint16_t, 1);
            if (cases[used].payload == NULL || cases[used].offsets == NULL ||
                cases[used].byte_offsets == NULL) {
                return false;
            }
            used++;
        }
        type->cases = cases;
        type->case_count = used;
    }
    return true;
}

static bool resolve_enum_cases(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_ENUM) {
            continue;
        }
        const char *name = qualified(program, decl->name);
        KestType *type = kest_find_type(program, name, strlen(name));
        if (type == NULL || type->cases != NULL) {
            continue;
        }

        uint32_t count = decl->choice.case_count;
        KestVariantType *cases = KEST_ARENA_ARRAY(program->arena,
                                                  KestVariantType,
                                                  count == 0 ? 1 : count);
        if (cases == NULL) {
            return false;
        }

        uint32_t used = 0;
        for (uint32_t c = 0; c < count; c++) {
            const KestVariant *written = decl->choice.cases[c];
            const char *case_name = span_string(program, written->name);
            if (case_name == NULL) {
                return false;
            }
            bool duplicate = false;
            for (uint32_t seen = 0; seen < used; seen++) {
                if (strcmp(cases[seen].name, case_name) == 0) {
                    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0303",
                                   written->name,
                                   "case `%s` is declared twice in `%s`",
                                   case_name, name);
                    kest_diags_note(program->diags, NULL, cases[seen].span,
                                    "the first one");
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }

            uint32_t held = written->payload_count;
            cases[used].name = case_name;
            cases[used].span = written->name;
            cases[used].payload_count = held;
            cases[used].payload =
                KEST_ARENA_ARRAY(program->arena, KestType *, held == 0 ? 1 : held);
            cases[used].offsets =
                KEST_ARENA_ARRAY(program->arena, uint16_t, held == 0 ? 1 : held);
            cases[used].byte_offsets =
                KEST_ARENA_ARRAY(program->arena, uint16_t, held == 0 ? 1 : held);
            if (cases[used].payload == NULL || cases[used].offsets == NULL ||
                cases[used].byte_offsets == NULL) {
                return false;
            }
            for (uint32_t p = 0; p < held; p++) {
                cases[used].payload[p] =
                    kest_resolve_type_ref(program, written->payload[p]);
            }
            used++;
        }
        type->cases = cases;
        type->case_count = used;
    }
    return true;
}

// The same type with every type name replaced by what it stands for. A type
// that mentions none is itself, so nothing is rebuilt for the common case.
bool kest_mentions_name(const KestType *type);

static bool mentions_param(const KestType *type) {
    if (type == NULL) {
        return false;
    }
    if (type->tag == KEST_T_PARAM) {
        return true;
    }
    if (mentions_param(type->element) || mentions_param(type->result)) {
        return true;
    }
    for (uint32_t i = 0; i < type->param_count; i++) {
        if (mentions_param(type->params[i])) {
            return true;
        }
    }
    // A copy of a generic struct is asked about what it was made with, not
    // about its fields: a struct that holds a reference to itself would have
    // no end.
    for (uint32_t i = 0; i < type->type_arg_count; i++) {
        if (mentions_param(type->type_args[i])) {
            return true;
        }
    }
    return false;
}

bool kest_mentions_name(const KestType *type) {
    return mentions_param(type);
}

KestType *kest_substitute(KestProgram *program, KestType *type,
                          const char **names, KestType **bindings,
                          uint32_t count) {
    if (type == NULL || !mentions_param(type)) {
        return type;
    }
    if (type->tag == KEST_T_PARAM) {
        for (uint32_t i = 0; i < count; i++) {
            if (strcmp(names[i], type->name) == 0) {
                return bindings[i];
            }
        }
        return type;
    }
    switch (type->tag) {
    case KEST_T_ARRAY:
        return kest_array_of(
            program,
            kest_substitute(program, type->element, names, bindings, count));
    case KEST_T_OPTIONAL:
        return kest_optional_of(
            program,
            kest_substitute(program, type->element, names, bindings, count));
    case KEST_T_REF:
        return kest_ref_of(
            program,
            kest_substitute(program, type->element, names, bindings, count));
    case KEST_T_STORE:
        return compose(
            program, KEST_T_STORE,
            kest_substitute(program, type->element, names, bindings, count));
    case KEST_T_FN: {
        KestType *params[16];
        uint32_t used = type->param_count < 16 ? type->param_count : 16;
        for (uint32_t i = 0; i < used; i++) {
            params[i] = kest_substitute(program, type->params[i], names,
                                        bindings, count);
        }
        return kest_fn_of(
            program, params, used,
            kest_substitute(program, type->result, names, bindings, count),
            type->no_alloc);
    }
    case KEST_T_STRUCT: {
        if (type->shape == NULL) {
            return type;
        }
        KestType *args[8];
        uint32_t used = type->type_arg_count < 8 ? type->type_arg_count : 8;
        for (uint32_t i = 0; i < used; i++) {
            args[i] = kest_substitute(program, type->type_args[i], names,
                                      bindings, count);
        }
        return kest_struct_of(program, type->shape, args, used);
    }
    default:
        return type;
    }
}

// Works out what a type name has to stand for by putting the declared type
// beside the one that was passed. Anything that does not mention a name is
// checked later, against the instance, where a mismatch reports properly.
bool kest_unify(const KestType *declared, const KestType *given,
                const char **names, KestType **bindings, uint32_t count) {
    if (declared == NULL || given == NULL) {
        return true;
    }
    if (declared->tag == KEST_T_PARAM) {
        for (uint32_t i = 0; i < count; i++) {
            if (strcmp(names[i], declared->name) != 0) {
                continue;
            }
            if (bindings[i] == NULL) {
                bindings[i] = (KestType *)given;
                return true;
            }
            return kest_type_equal(given, bindings[i]);
        }
        return true;
    }
    if (declared->tag != given->tag) {
        return true;
    }
    if (declared->tag == KEST_T_STRUCT) {
        if (declared->shape == NULL || declared->shape != given->shape) {
            return true;
        }
        for (uint32_t i = 0; i < declared->type_arg_count &&
                             i < given->type_arg_count;
             i++) {
            if (!kest_unify(declared->type_args[i], given->type_args[i], names,
                            bindings, count)) {
                return false;
            }
        }
        return true;
    }
    switch (declared->tag) {
    case KEST_T_ARRAY:
    case KEST_T_OPTIONAL:
    case KEST_T_REF:
    case KEST_T_STORE:
        return kest_unify(declared->element, given->element, names, bindings,
                          count);
    case KEST_T_FN: {
        if (declared->param_count != given->param_count) {
            return true;
        }
        for (uint32_t i = 0; i < declared->param_count; i++) {
            if (!kest_unify(declared->params[i], given->params[i], names,
                            bindings, count)) {
                return false;
            }
        }
        return kest_unify(declared->result, given->result, names, bindings,
                          count);
    }
    default:
        return true;
    }
}

// Binds the names a generic declaration brought into scope. Anything resolved
// while they are bound sees them; nothing else does.
void kest_bind_types(KestProgram *program, const char **names,
                     KestType **types, uint32_t count) {
    program->bound_count = count > 8 ? 8 : count;
    for (uint32_t i = 0; i < program->bound_count; i++) {
        program->bound_names[i] = names[i];
        program->bound_types[i] = types[i];
    }
}

void kest_unbind_types(KestProgram *program) {
    program->bound_count = 0;
}

// One copy per set of types. Asking twice for the same set gives the one that
// is already there, so a call in a loop compiles one body.
KestInstance *kest_instance_of(KestProgram *program, const KestDecl *decl,
                               const KestUnitInfo *unit, const char **names,
                               KestType **bindings, uint32_t count) {
    for (uint32_t i = 0; i < program->instance_count; i++) {
        KestInstance *held = &program->instances[i];
        if (held->decl != decl || held->count != count) {
            continue;
        }
        bool same = true;
        for (uint32_t b = 0; b < count && same; b++) {
            same = kest_type_equal(held->bindings[b], bindings[b]) &&
                   kest_type_equal(bindings[b], held->bindings[b]);
        }
        if (same) {
            return held;
        }
    }
    if (program->instance_count == program->instance_capacity) {
        uint32_t capacity = program->instance_capacity == 0
                                ? 8
                                : program->instance_capacity * 2;
        KestInstance *grown =
            KEST_ARENA_ARRAY(program->arena, KestInstance, capacity);
        if (grown == NULL) {
            return NULL;
        }
        if (program->instance_count > 0) {
            memcpy(grown, program->instances,
                   (size_t)program->instance_count * sizeof(KestInstance));
        }
        program->instances = grown;
        program->instance_capacity = capacity;
    }
    KestInstance *made = &program->instances[program->instance_count++];
    memset(made, 0, sizeof *made);
    made->decl = decl;
    made->unit = unit;
    made->count = count > 8 ? 8 : count;
    for (uint32_t i = 0; i < made->count; i++) {
        made->names[i] = names[i];
        made->bindings[i] = bindings[i];
    }
    return made;
}

static bool declare_functions(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_FN) {
            continue;
        }

        KestType *type = new_type(program, KEST_T_FN);
        if (type == NULL) {
            return false;
        }

        // The signature of a generic function mentions names that stand for
        // themselves until a call says what they are.
        const char *names[8];
        KestType *stands[8];
        uint32_t generics = decl->type_param_count > 8
                                ? 8
                                : decl->type_param_count;
        for (uint32_t g = 0; g < generics; g++) {
            names[g] = span_string(program, decl->type_params[g]);
            stands[g] = new_type(program, KEST_T_PARAM);
            if (names[g] == NULL || stands[g] == NULL) {
                return false;
            }
            stands[g]->name = names[g];
            stands[g]->slots = 1;
            stands[g]->byte_size = 8;
            stands[g]->byte_align = 8;
        }
        kest_bind_types(program, names, stands, generics);
        type->type_param_count = generics;
        if (generics > 0) {
            type->type_param_names =
                KEST_ARENA_ARRAY(program->arena, const char *, generics);
            if (type->type_param_names == NULL) {
                return false;
            }
            for (uint32_t g = 0; g < generics; g++) {
                type->type_param_names[g] = names[g];
            }
            type->decl = decl;
            type->unit = program->unit;
        }

        uint32_t count = decl->function.param_count;
        type->params =
            KEST_ARENA_ARRAY(program->arena, KestType *, count == 0 ? 1 : count);
        if (type->params == NULL) {
            return false;
        }
        type->param_count = count;

        for (uint32_t p = 0; p < count; p++) {
            const KestField *param = decl->function.params[p];
            type->params[p] = kest_resolve_type_ref(program, param->type);

            const char *param_name = span_string(program, param->name);
            for (uint32_t seen = 0; seen < p; seen++) {
                const KestField *earlier = decl->function.params[seen];
                if (earlier->name.length == param->name.length &&
                    memcmp(program->source->text + earlier->name.offset,
                           param_name, param->name.length) == 0) {
                    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0305",
                                   param->name,
                                   "parameter `%s` is declared twice",
                                   param_name);
                    kest_diags_note(program->diags, NULL, earlier->name,
                                    "the first one");
                    break;
                }
            }
        }

        type->result = decl->function.result == NULL
                           ? kest_find_type(program, "void", 4)
                           : kest_resolve_type_ref(program, decl->function.result);
        type->no_alloc = decl->function.no_alloc;
        type->is_foreign = decl->function.is_extern;

        // An extern function with a receiver is named for the host type it
        // belongs to, so `Clock.now` and `Timer.now` can both exist.
        KestSpan span = decl->name;
        const char *name;
        KestSpan bare = decl->name;
        if (decl->function.receiver.length > 0) {
            bare.offset = decl->function.receiver.offset;
            bare.length = decl->name.offset + decl->name.length -
                          decl->function.receiver.offset;
        }
        type->foreign_name = span_string(program, bare);
        if (decl->function.receiver.length > 0) {
            KestSpan whole = {decl->function.receiver.offset,
                              decl->name.offset + decl->name.length -
                                  decl->function.receiver.offset};
            span = whole;
            name = qualified(program, whole);
        } else {
            name = qualified(program, decl->name);
        }
        if (name == NULL) {
            return false;
        }
        type->symbol = symbol_of(program, name, type);
        kest_unbind_types(program);
        if (!add_global(program, name, type, span, true)) {
            return false;
        }
    }
    return true;
}

static bool declare_constants(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_CONST) {
            continue;
        }
        const char *name = qualified(program, decl->name);
        KestType *type = kest_resolve_type_ref(program, decl->constant.type);
        // What it is written as, kept so that working it out is possible
        // wherever it is used and wherever a count asks for it.
        if (name == NULL || !add_global_value(program, name, type, decl->name,
                                              true, decl->constant.value)) {
            return false;
        }
    }
    return true;
}

const char *kest_nearest_global(KestProgram *program, const char *name,
                                size_t length) {
    if (length < 3) {
        return NULL;
    }
    uint32_t limit = length == 3 ? 1 : (uint32_t)length / 3;
    const char *best = NULL;
    uint32_t best_distance = limit + 1;

    for (uint32_t i = 0; i < program->global_count; i++) {
        const char *candidate = program->globals[i].name;
        uint32_t distance =
            edit_distance(name, length, candidate, strlen(candidate), limit);
        if (distance < best_distance) {
            best_distance = distance;
            best = candidate;
        }
    }
    return best;
}

const char *kest_nearest_member(const KestType *type, const char *name,
                                size_t length) {
    if (length < 3) {
        return NULL;
    }
    uint32_t limit = length == 3 ? 1 : (uint32_t)length / 3;
    const char *best = NULL;
    uint32_t best_distance = limit + 1;

    for (uint32_t i = 0; i < type->member_count; i++) {
        const char *candidate = type->members[i].name;
        uint32_t distance =
            edit_distance(name, length, candidate, strlen(candidate), limit);
        if (distance < best_distance) {
            best_distance = distance;
            best = candidate;
        }
    }
    return best;
}

bool kest_type_equal(const KestType *a, const KestType *b) {
    if (a == NULL || b == NULL) {
        return true;
    }
    if (a == b) {
        return true;
    }
    if (a->tag == KEST_T_ERROR || b->tag == KEST_T_ERROR) {
        return true;
    }
    if (a->tag != b->tag) {
        return false;
    }
    switch (a->tag) {
    case KEST_T_INT:
        return a->width == b->width && a->is_signed == b->is_signed;
    case KEST_T_FLOAT:
        return a->width == b->width;
    case KEST_T_ARRAY:
    case KEST_T_REF:
    case KEST_T_STORE:
    case KEST_T_OPTIONAL:
        return kest_type_equal(a->element, b->element);
    case KEST_T_FIXED:
        return a->count == b->count && kest_type_equal(a->element, b->element);
    case KEST_T_FN: {
        // Called as (given, wanted): a value that promises more fits where
        // less is asked for.
        if (a->param_count != b->param_count ||
            !kest_type_equal(a->result, b->result)) {
            return false;
        }
        for (uint32_t i = 0; i < a->param_count; i++) {
            if (!kest_type_equal(a->params[i], b->params[i])) {
                return false;
            }
        }
        return a->no_alloc || !b->no_alloc;
    }
    default:
        // Primitives and structs are unique, so anything left that did not
        // match by pointer is a different type.
        return false;
    }
}

bool kest_check(KestArena *arena, KestDiags *diags, const KestUnits *units,
                KestProgram **out) {
    KestProgram *program = KEST_ARENA_NEW(arena, KestProgram);
    if (program == NULL) {
        return false;
    }
    program->arena = arena;
    program->diags = diags;
    program->alias = "";
    *out = program;

    // Nothing is declared for a program before it says what it imports.
    // Saying something is the host's to do and `std.io` is where it is asked
    // for; see D024.
    if (!add_primitives(program)) {
        return false;
    }

    // Names live under the last part of a module's name, so two modules whose
    // names end the same way would share one. Nothing tells them apart yet,
    // and pretending otherwise would put one file's names in another's.
    for (uint32_t i = 0; i < units->count; i++) {
        if (units->items[i].alias[0] == '\0') {
            continue;
        }
        for (uint32_t j = 0; j < i; j++) {
            if (strcmp(units->items[i].alias, units->items[j].alias) != 0) {
                continue;
            }
            kest_diags_in(diags, &units->items[i].source);
            KestSpan span = {0, 1};
            for (uint32_t d = 0; d < units->items[i].unit.count; d++) {
                if (units->items[i].unit.items[d]->kind == KEST_DECL_MODULE) {
                    span = units->items[i].unit.items[d]->name;
                }
            }
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0328",
                           span, "two modules both put their names under `%s`",
                           units->items[i].alias);
            KestSpan other = {0, 1};
            for (uint32_t d = 0; d < units->items[j].unit.count; d++) {
                if (units->items[j].unit.items[d]->kind == KEST_DECL_MODULE) {
                    other = units->items[j].unit.items[d]->name;
                }
            }
            kest_diags_note(diags, &units->items[j].source, other,
                            "the other one");
        }
    }

    // Every struct in every file is registered before any field is resolved,
    // so a type may name one declared in a file that has not been read yet as
    // well as one below it.
    for (uint32_t i = 0; i < units->count; i++) {
        kest_program_in(program, &units->items[i]);
        kest_diags_in(diags, program->source);
        if (!declare_structs(program, &units->items[i].unit) ||
            !declare_enums(program, &units->items[i].unit) ||
            !declare_flags(program, &units->items[i].unit)) {
            return false;
        }
    }
    for (uint32_t i = 0; i < units->count; i++) {
        kest_program_in(program, &units->items[i]);
        kest_diags_in(diags, program->source);
        if (!resolve_struct_fields(program, &units->items[i].unit) ||
            !resolve_enum_cases(program, &units->items[i].unit) ||
            !resolve_flags_cases(program, &units->items[i].unit)) {
            return false;
        }
    }
    measure_all(program);
    for (uint32_t i = 0; i < units->count; i++) {
        kest_program_in(program, &units->items[i]);
        kest_diags_in(diags, program->source);
        if (!declare_constants(program, &units->items[i].unit) ||
            !declare_functions(program, &units->items[i].unit)) {
            return false;
        }
    }
    return true;
}

void kest_program_dump(const KestProgram *program, KestArena *arena,
                       FILE *out) {
    for (uint32_t i = 0; i < program->type_count; i++) {
        const KestType *type = program->types[i];
        // A shape is not a type and has no layout, and neither has a copy
        // made with a name that is still standing for itself.
        if (type->type_param_count > 0 || mentions_param(type)) {
            continue;
        }
        if (type->tag == KEST_T_FLAGS) {
            fprintf(out, "flags %s  1 slot, %u byte%s over u%u\n", type->name,
                    type->byte_size, type->byte_size == 1 ? "" : "s",
                    type->width);
            for (uint32_t c = 0; c < type->case_count; c++) {
                fprintf(out, "  bit %u  %s\n", c, type->cases[c].name);
            }
            continue;
        }
        if (type->tag == KEST_T_ENUM) {
            fprintf(out, "enum %s  %u slot%s, %u byte%s aligned %u\n",
                    type->name, type->slots, type->slots == 1 ? "" : "s",
                    type->byte_size, type->byte_size == 1 ? "" : "s",
                    type->byte_align);
            for (uint32_t c = 0; c < type->case_count; c++) {
                fprintf(out, "  %u %s", c, type->cases[c].name);
                for (uint32_t p = 0; p < type->cases[c].payload_count; p++) {
                    fprintf(out, " slot +%u byte +%u %s",
                            type->cases[c].offsets[p],
                            type->cases[c].byte_offsets[p],
                            kest_type_name(arena, type->cases[c].payload[p]));
                }
                fputc('\n', out);
            }
            continue;
        }
        if (type->tag != KEST_T_STRUCT) {
            continue;
        }
        fprintf(out, "struct %s  %u slot%s, %u byte%s aligned %u\n",
                type->name, type->slots, type->slots == 1 ? "" : "s",
                type->byte_size, type->byte_size == 1 ? "" : "s",
                type->byte_align);
        for (uint32_t m = 0; m < type->member_count; m++) {
            fprintf(out, "  slot +%u  byte +%-3u %s: %s\n",
                    type->members[m].offset, type->members[m].byte_offset,
                    type->members[m].name,
                    kest_type_name(arena, type->members[m].type));
        }
    }

    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestSymbol *symbol = &program->globals[i];
        const KestType *type = symbol->type;
        if (type->tag != KEST_T_FN) {
            fprintf(out, "const %s: %s\n", symbol->name,
                    kest_type_name(arena, type));
            continue;
        }
        fprintf(out, "fn %s(", symbol->name);
        for (uint32_t p = 0; p < type->param_count; p++) {
            fprintf(out, "%s%s", p > 0 ? ", " : "",
                    kest_type_name(arena, type->params[p]));
        }
        fprintf(out, ") -> %s%s\n", kest_type_name(arena, type->result),
                type->no_alloc ? " no.alloc" : "");
    }
}


// Where something was declared, which is what a tool wants in order to go
// there.
static void write_where(const KestSource *source, KestSpan span, FILE *out) {
    if (source == NULL) {
        return;
    }
    uint32_t line = 0;
    uint32_t column = 0;
    kest_source_locate(source, span.offset, &line, &column);
    fputs(",\"file\":", out);
    kest_json_text(source->path, out);
    fprintf(out, ",\"line\":%u,\"column\":%u", line, column);
}

void kest_program_dump_json(const KestProgram *program, KestArena *arena,
                            FILE *out) {
    fputs("\"types\":[", out);
    bool first = true;
    for (uint32_t i = 0; i < program->type_count; i++) {
        const KestType *type = program->types[i];
        if (type->tag != KEST_T_STRUCT && type->tag != KEST_T_ENUM) {
            continue;
        }
        fputs(first ? "" : ",", out);
        first = false;
        fputs("{\"name\":", out);
        kest_json_text(type->name, out);
        fprintf(out, ",\"kind\":\"%s\"",
                type->tag == KEST_T_ENUM ? "enum" : "struct");
        fprintf(out, ",\"slots\":%u,\"bytes\":%u,\"align\":%u", type->slots,
                type->byte_size, type->byte_align);
        write_where(type->declared_in, type->span, out);
        if (type->tag == KEST_T_ENUM) {
            // What a case carries and where each piece of it sits, which is
            // what a host laying one out beside its own needs.
            fputs(",\"cases\":[", out);
            for (uint32_t c = 0; c < type->case_count; c++) {
                fputs(c == 0 ? "" : ",", out);
                fputs("{\"name\":", out);
                kest_json_text(type->cases[c].name, out);
                fprintf(out, ",\"tag\":%u,\"carries\":[", c);
                for (uint32_t p = 0; p < type->cases[c].payload_count; p++) {
                    fputs(p == 0 ? "" : ",", out);
                    fputs("{\"type\":", out);
                    kest_json_text(
                        kest_type_name(arena, type->cases[c].payload[p]), out);
                    fprintf(out, ",\"slot\":%u,\"byte\":%u}",
                            type->cases[c].offsets[p],
                            type->cases[c].byte_offsets[p]);
                }
                fputs("]}", out);
            }
            fputs("]}", out);
            continue;
        }
        fputs(",\"fields\":[", out);
        for (uint32_t m = 0; m < type->member_count; m++) {
            fputs(m == 0 ? "" : ",", out);
            fputs("{\"name\":", out);
            kest_json_text(type->members[m].name, out);
            fputs(",\"type\":", out);
            kest_json_text(kest_type_name(arena, type->members[m].type), out);
            fprintf(out, ",\"slot\":%u,\"byte\":%u}",
                    type->members[m].offset, type->members[m].byte_offset);
        }
        fputs("]}", out);
    }

    fputs("],\"functions\":[", out);
    first = true;
    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestSymbol *symbol = &program->globals[i];
        if (symbol->type->tag != KEST_T_FN) {
            continue;
        }
        fputs(first ? "" : ",", out);
        first = false;
        fputs("{\"name\":", out);
        kest_json_text(symbol->name, out);
        fputs(",\"parameters\":[", out);
        for (uint32_t p = 0; p < symbol->type->param_count; p++) {
            fputs(p == 0 ? "" : ",", out);
            kest_json_text(kest_type_name(arena, symbol->type->params[p]), out);
        }
        fputs("],\"result\":", out);
        kest_json_text(kest_type_name(arena, symbol->type->result), out);
        fprintf(out, ",\"noAlloc\":%s,\"foreign\":%s",
                symbol->type->no_alloc ? "true" : "false",
                symbol->type->is_foreign ? "true" : "false");
        write_where(symbol->source, symbol->span, out);
        fputc('}', out);
    }

    fputs("],\"constants\":[", out);
    first = true;
    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestSymbol *symbol = &program->globals[i];
        if (symbol->type->tag == KEST_T_FN) {
            continue;
        }
        fputs(first ? "" : ",", out);
        first = false;
        fputs("{\"name\":", out);
        kest_json_text(symbol->name, out);
        fputs(",\"type\":", out);
        kest_json_text(kest_type_name(arena, symbol->type), out);
        write_where(symbol->source, symbol->span, out);
        fputc('}', out);
    }
    fputc(']', out);
}
