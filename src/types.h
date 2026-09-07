#ifndef KEST_TYPES_H
#define KEST_TYPES_H

#include "ast.h"

typedef enum {
    // Stands in where a type could not be resolved. It compares equal to
    // everything, so one bad annotation reports once instead of at every use.
    KEST_T_ERROR,
    KEST_T_VOID,
    KEST_T_BOOL,
    KEST_T_INT,
    KEST_T_FLOAT,
    KEST_T_TEXT,
    KEST_T_STRUCT,
    KEST_T_ARRAY,
    KEST_T_REF,
    KEST_T_OPTIONAL,
    KEST_T_FN,
} KestTypeTag;

typedef struct KestType KestType;

typedef struct {
    const char *name;
    KestType *type;
    KestSpan span;
} KestMember;

struct KestType {
    KestTypeTag tag;
    // Set for primitives and structs. Composed types are named on demand by
    // kest_type_name, so nothing has to be built for types nobody reports.
    const char *name;
    // INT and FLOAT.
    uint8_t width;
    bool is_signed;
    // STRUCT.
    KestMember *members;
    uint32_t member_count;
    KestSpan span;
    // ARRAY, REF and OPTIONAL.
    KestType *element;
    // FN.
    KestType **params;
    uint32_t param_count;
    KestType *result;
    bool no_alloc;
};

typedef struct {
    const char *name;
    KestType *type;
    KestSpan span;
    bool is_const;
} KestSymbol;

// Everything one file declares, after names have been resolved to types.
typedef struct KestProgram KestProgram;

// Resolves declarations, their field types and their signatures, reporting
// what it cannot resolve. Returns false only when the host is out of memory.
bool kest_check(KestArena *arena, const KestSource *source, KestDiags *diags,
                const KestUnit *unit, KestProgram **out);

// The spelling used in diagnostics: `i32`, `[Player]`, `ref<Npc>?`.
const char *kest_type_name(KestArena *arena, const KestType *type);

// Prints what was resolved, for seeing what the checker built.
void kest_program_dump(const KestProgram *program, KestArena *arena, FILE *out);

#endif
