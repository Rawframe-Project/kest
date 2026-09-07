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

    KestType *type = kest_lookup_type(program, name, length);
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

static bool add_global(KestProgram *program, const char *name, KestType *type,
                       KestSpan span, bool is_const) {
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
    uint16_t bytes = 0;
    uint16_t align = 1;
    for (uint32_t i = 0; i < type->member_count; i++) {
        KestType *member = type->members[i].type;
        if (member != NULL && member->tag == KEST_T_STRUCT &&
            !measure_struct(program, member)) {
            type->sizing = false;
            // One slot, so the rest of the file is still checkable against a
            // type that has a size even though it is the wrong one.
            type->slots = 1;
            type->byte_size = 8;
            type->byte_align = 8;
            return false;
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
    type->sizing = false;
    type->slots = offset == 0 ? 1 : offset;
    type->byte_align = align;
    type->byte_size = bytes == 0 ? 1 : (uint16_t)((bytes + align - 1) / align *
                                                  align);
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
        if (name == NULL || !add_global(program, name, type, decl->name, true)) {
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
        if (!declare_structs(program, &units->items[i].unit)) {
            return false;
        }
    }
    for (uint32_t i = 0; i < units->count; i++) {
        kest_program_in(program, &units->items[i]);
        kest_diags_in(diags, program->source);
        if (!resolve_struct_fields(program, &units->items[i].unit)) {
            return false;
        }
    }
    measure_structs(program);
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
