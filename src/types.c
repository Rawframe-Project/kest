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

KestType *kest_find_type(KestProgram *program, const char *name,
                         size_t length) {
    for (uint32_t i = 0; i < program->type_count; i++) {
        const char *candidate = program->types[i]->name;
        if (strlen(candidate) == length &&
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
const char *kest_nearest_type(KestProgram *program, const char *name,
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
    return type;
}

static KestType *error_type(KestProgram *program) {
    return new_type(program, KEST_T_ERROR);
}

static KestType *resolve_named(KestProgram *program, const KestTypeRef *ref) {
    const char *name = program->source->text + ref->name.offset;
    size_t length = ref->name.length;

    KestType *type = kest_find_type(program, name, length);
    if (type != NULL) {
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

KestType *kest_resolve_type_ref(KestProgram *program,
                                const KestTypeRef *ref) {
    if (ref == NULL) {
        return error_type(program);
    }

    switch (ref->kind) {
    case KEST_TYPE_NAMED:
        return resolve_named(program, ref);

    case KEST_TYPE_GENERIC: {
        const char *name = program->source->text + ref->name.offset;
        bool is_ref = ref->name.length == 3 && memcmp(name, "ref", 3) == 0;
        bool is_store = ref->name.length == 5 && memcmp(name, "store", 5) == 0;
        if (!is_ref && !is_store) {
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

    case KEST_TYPE_ARRAY:
        return compose(program, KEST_T_ARRAY,
                       kest_resolve_type_ref(program, ref->element));

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

    const char *inner = kest_type_name(arena, type->element);
    char buffer[256];
    switch (type->tag) {
    case KEST_T_ARRAY:
        snprintf(buffer, sizeof(buffer), "[%s]", inner);
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

static bool add_global(KestProgram *program, const char *name, KestType *type,
                       KestSpan span, bool is_const) {
    KestSymbol *existing = kest_find_global(program, name, strlen(name));
    if (existing != NULL) {
        kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0304", span,
                       "`%s` is already declared in this file", name);
        uint32_t line = 0;
        uint32_t column = 0;
        kest_source_locate(program->source, existing->span.offset, &line,
                           &column);
        kest_diags_suggest(program->diags, "the first is on line %u", line);
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
    symbol->is_const = is_const;
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
        const char *name = span_string(program, decl->name);
        if (name == NULL) {
            return false;
        }
        if (kest_find_type(program, name, strlen(name)) != NULL) {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0304",
                           decl->name, "`%s` is already declared in this file",
                           name);
            continue;
        }
        KestType *type = new_type(program, KEST_T_STRUCT);
        if (type == NULL || !register_type(program, type)) {
            return false;
        }
        type->name = name;
        type->span = decl->name;
    }
    return true;
}

static bool resolve_struct_fields(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_STRUCT) {
            continue;
        }
        const char *name = span_string(program, decl->name);
        KestType *type = kest_find_type(program, name, strlen(name));
        if (type == NULL || type->members != NULL) {
            continue;
        }

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
    }
    return true;
}

// Lays a struct out flat and reports one that contains itself, which has no
// size to compute and would otherwise be followed forever.
static bool measure_struct(KestProgram *program, KestType *type) {
    if (type->slots > 0 || type->tag != KEST_T_STRUCT) {
        return true;
    }
    if (type->sizing) {
        kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0319", type->span,
                       "`%s` contains itself, so it has no size", type->name);
        kest_diags_suggest(program->diags,
                           "hold it through `ref<%s>`, which is a handle",
                           type->name);
        return false;
    }

    type->sizing = true;
    uint16_t offset = 0;
    for (uint32_t i = 0; i < type->member_count; i++) {
        KestType *member = type->members[i].type;
        if (member != NULL && member->tag == KEST_T_STRUCT &&
            !measure_struct(program, member)) {
            type->sizing = false;
            // One slot, so the rest of the file is still checkable against a
            // type that has a size even though it is the wrong one.
            type->slots = 1;
            return false;
        }
        type->members[i].offset = offset;
        offset += member == NULL ? 1 : member->slots;
    }
    type->sizing = false;
    type->slots = offset == 0 ? 1 : offset;
    return true;
}

static bool measure_structs(KestProgram *program) {
    for (uint32_t i = 0; i < program->type_count; i++) {
        measure_struct(program, program->types[i]);
    }
    return true;
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
                    break;
                }
            }
        }

        type->result = decl->function.result == NULL
                           ? kest_find_type(program, "void", 4)
                           : kest_resolve_type_ref(program, decl->function.result);
        type->no_alloc = decl->function.no_alloc;

        // An extern function with a receiver is named for the host type it
        // belongs to, so `Clock.now` and `Timer.now` can both exist.
        KestSpan span = decl->name;
        const char *name;
        if (decl->function.receiver.length > 0) {
            KestSpan whole = {decl->function.receiver.offset,
                              decl->name.offset + decl->name.length -
                                  decl->function.receiver.offset};
            span = whole;
            name = span_string(program, whole);
        } else {
            name = span_string(program, decl->name);
        }
        if (name == NULL || !add_global(program, name, type, span, true)) {
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
        const char *name = span_string(program, decl->name);
        KestType *type = kest_resolve_type_ref(program, decl->constant.type);
        if (name == NULL || !add_global(program, name, type, decl->name, true)) {
            return false;
        }
    }
    return true;
}

// The one function a program can call before anything is imported. It exists
// so a program can be run and looked at; a standard library replaces it.
static bool add_builtins(KestProgram *program) {
    KestType *type = new_type(program, KEST_T_FN);
    KestType **params = KEST_ARENA_ARRAY(program->arena, KestType *, 1);
    if (type == NULL || params == NULL) {
        return false;
    }
    params[0] = kest_find_type(program, "text", 4);
    type->params = params;
    type->param_count = 1;
    type->result = kest_find_type(program, "void", 4);

    KestSpan nowhere = {0, 0};
    return add_global(program, "print", type, nowhere, true);
}

static bool declare_imports(KestProgram *program, const KestUnit *unit) {
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_IMPORT) {
            continue;
        }
        KestType *type = new_type(program, KEST_T_MODULE);
        const char *name = span_string(program, decl->name);
        if (type == NULL || name == NULL) {
            return false;
        }
        type->name = name;
        if (!add_global(program, name, type, decl->name, true)) {
            return false;
        }
    }
    return true;
}

bool kest_check(KestArena *arena, const KestSource *source, KestDiags *diags,
                const KestUnit *unit, KestProgram **out) {
    KestProgram *program = KEST_ARENA_NEW(arena, KestProgram);
    if (program == NULL) {
        return false;
    }
    program->arena = arena;
    program->source = source;
    program->diags = diags;

    *out = program;

    return add_primitives(program) && add_builtins(program) &&
           declare_imports(program, unit) &&
           declare_structs(program, unit) &&
           resolve_struct_fields(program, unit) && measure_structs(program) &&
           declare_constants(program, unit) && declare_functions(program, unit);
}

const char *kest_nearest_global(KestProgram *program, const char *name,
                                size_t length) {
    // Every one or two character name is one edit from every other, so a
    // suggestion at that length carries no information.
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
    // Every one or two character name is one edit from every other, so a
    // suggestion at that length carries no information.
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
    default:
        // Primitives and structs are unique, so anything left that did not
        // match by pointer is a different type.
        return false;
    }
}

void kest_program_dump(const KestProgram *program, KestArena *arena,
                       FILE *out) {
    for (uint32_t i = 0; i < program->type_count; i++) {
        const KestType *type = program->types[i];
        if (type->tag != KEST_T_STRUCT) {
            continue;
        }
        fprintf(out, "struct %s  %u slot%s\n", type->name, type->slots,
                type->slots == 1 ? "" : "s");
        for (uint32_t m = 0; m < type->member_count; m++) {
            fprintf(out, "  +%u %s: %s\n", type->members[m].offset,
                    type->members[m].name,
                    kest_type_name(arena, type->members[m].type));
        }
    }

    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestSymbol *symbol = &program->globals[i];
        const KestType *type = symbol->type;
        if (type->tag == KEST_T_MODULE) {
            fprintf(out, "import %s\n", symbol->name);
            continue;
        }
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
