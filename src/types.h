#ifndef KEST_TYPES_H
#define KEST_TYPES_H

#include "kest.h"
#include "loader.h"

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
    KEST_T_ENUM,
    // A set of named bits over an integer of a written width. Not an enum: a
    // value is any combination of the cases, so nothing exhausts it and a
    // `match` does not apply. See D033.
    KEST_T_FLAGS,
    KEST_T_ARRAY,
    // `[f32; 16]`: that many, laid out where it stands, and a value like a
    // struct rather than a handle like an array. See D064.
    KEST_T_FIXED,
    KEST_T_REF,
    // A slot map that hands out references and can delete what it holds. Not
    // a collector, not a count, not a region: see D014.
    KEST_T_STORE,
    KEST_T_OPTIONAL,
    KEST_T_FN,
    // An imported name. Its members are not resolved yet, so reading one
    // yields an error type without a diagnostic; see the worklog.
    KEST_T_MODULE,
    // A name standing for one type per instance of a generic function. It
    // never reaches the compiler: a call binds it and a copy is compiled with
    // it substituted. See D040.
    KEST_T_PARAM,
} KestTypeTag;

typedef struct {
    const char *name;
    KestType *type;
    KestSpan span;
    // Where this member starts inside its struct, in slots. A struct is a
    // value laid out flat, so a nested struct's members are part of the same
    // run and a field is reached by adding offsets rather than by chasing a
    // pointer.
    uint16_t offset;
    // And where it starts in bytes, which is a different number: a slot is
    // eight bytes whatever it holds, and a `f32` is four. The two layouts are
    // for two places, and D016 says which is which.
    uint16_t byte_offset;
} KestMember;

// One case of an enum: what it carries, by position, and where each piece
// sits. The tag is slot zero and byte zero, so every case's payload starts
// after it and the tag can be read without knowing which case it is.
typedef struct {
    const char *name;
    KestType **payload;
    uint16_t *offsets;
    uint16_t *byte_offsets;
    uint32_t payload_count;
    KestSpan span;
    // Whether anything in the program wrote this one's name: built it, tested
    // for it, or answered it in a `match`. A set of bits and an enum are the
    // one place where a name inside a shape can go unwritten while the shape
    // itself is held everywhere.
    bool named;
} KestVariantType;

struct KestType {
    KestTypeTag tag;
    // Set for primitives and structs. Composed types are named on demand by
    // kest_type_name, so nothing has to be built for types nobody reports.
    const char *name;
    // ENUM.
    KestVariantType *cases;
    // STRUCT.
    KestMember *members;
    // Which file declared it. A primitive has none.
    const KestSource *declared_in;
    // ARRAY, REF, OPTIONAL and FIXED.
    KestType *element;
    // FN.
    KestType **params;
    KestType *result;
    // The name this one function is compiled under, which includes what it
    // takes, because two functions may share a name if they take different
    // things. Set for every function, generic or not.
    const char *symbol;
    // A generic function, whose parameters mention type names. It has no body
    // to compile until a call says what they stand for, so where it was
    // written is kept: a call makes the copy from there.
    const char **type_param_names;
    const KestDecl *decl;
    const KestUnitInfo *unit;
    // A copy of a generic struct: which shape it came from and what it was
    // made with, so a copy inside a generic body can be made again with the
    // names that body was given.
    KestType *shape;
    KestType **type_args;
    // Declared rather than defined here, so the host must provide it and
    // nothing about it can be inferred. The name the host binds is the one
    // written, without the module: which file declared it is Kest's business
    // and not the host's.
    const char *foreign_name;
    // The words are all above and the numbers are all here, which is what
    // keeps a type from being half padding: a `bool` between two pointers is
    // seven bytes of nothing, and this shape had seven of those. See D644.
    KestSpan span;
    uint32_t case_count;
    uint32_t member_count;
    // FIXED only: how many.
    uint32_t count;
    uint32_t param_count;
    uint32_t type_param_count;
    uint32_t type_arg_count;
    // How many slots a value of this type occupies. One for everything that
    // fits in a machine word, and the sum of its members for a struct.
    uint16_t slots;
    // What this type is where memory is shared: the size and alignment a C
    // compiler would give it, so an array of them can be the same bytes the
    // host already has.
    uint16_t byte_size;
    uint16_t byte_align;
    // INT and FLOAT.
    uint8_t width;
    bool is_signed;
    // Set while the size is being worked out, so a struct that contains
    // itself is caught rather than followed forever.
    bool sizing;
    // Whether anything in the program wrote its name: a field, a parameter,
    // a binding, a value built out of it. A type nothing names is compiled,
    // laid out, and never reachable — see the warning `check` gives for it.
    bool named;
    bool no_alloc;
    bool is_foreign;
};

typedef struct KestInstance KestInstance;

typedef struct {
    const char *name;
    KestType *type;
    KestSpan span;
    // Which file declared it, so what is said about it can be shown there.
    const KestSource *source;
    // What was written, for the things the type does not carry: a parameter
    // has a name where it is declared and only a type after that. NULL for a
    // constant, which is a name for a value and has no parameters.
    const KestDecl *decl;
    bool is_const;
    // Whether anything in the program named it: a call, or the name handed
    // around as a value. What this is for is a library, where a function
    // nothing names is one nothing has ever run — a reader can only count
    // mentions, and a mention in a comment is not one.
    bool named;
    // What a constant is written as, for working it out. A constant is a name
    // for a value and the value is in the tree; nothing else needs this.
    const KestExpr *value;
} KestSymbol;

// Everything one file declares, after names have been resolved to types.
typedef struct {
    KestArena *arena;
    // The file being worked on, and the name its declarations live under.
    // Every name is registered qualified; inside its own module the prefix
    // may be left off, which is the only thing the alias is for.
    const KestSource *source;
    const char *alias;
    const KestUnitInfo *unit;
    KestDiags *diags;

    // Primitives and structs, in declaration order. A file declares few enough
    // types that a scan beats a hash table.
    KestType **types;
    uint32_t type_count;
    uint32_t type_capacity;

    KestSymbol *globals;
    uint32_t global_count;
    uint32_t global_capacity;
    // Where each of them is, by name. Unlike the types above, there are enough
    // of these for the difference to show: the list is walked by name for
    // every declaration and every use, so what it cost was the program's own
    // size squared. Slots hold one more than the place they name, so nought is
    // an empty slot. See D327.
    uint32_t *by_name;
    uint32_t by_name_slots;

    // What the type names in scope stand for right now. Only a generic
    // signature or a generic body is resolved with any of these set. As many
    // as a declaration wrote: this was a run of eight, and the ninth was
    // dropped without a word, so a shape's own type name was unknown between
    // its own angle brackets.
    const char **bound_names;
    KestType **bound_types;
    uint32_t bound_count;
    uint32_t bound_capacity;

    // One per set of types a generic function is called with. The checker
    // fills this and the compiler walks it, so a copy exists exactly where it
    // is used and nowhere else.
    KestInstance *instances;
    uint32_t instance_count;
    uint32_t instance_capacity;
    // Constants that a `[T; N]` counted with. A type is resolved before the
    // constants are declared — a struct's fields are what a constant of that
    // struct is measured from — so there is no symbol to mark when a count
    // reads one, and the name is kept until there is.
    const char **counted;
    uint32_t counted_count;
    uint32_t counted_capacity;
    // Every type this program made, counted where they are made. What is in
    // `types` is the ones with names; a program makes one for every signature,
    // every optional, every run of something, and those are most of them. A
    // tree was countable because the parser makes nodes in three places, and
    // this is the same for the stage above it. See D644.
    uint32_t types_made;
} KestProgram;

// A generic function with its type names bound. The symbol is what the copy
// is compiled under, which is the name with what it was given written into it.
struct KestInstance {
    const KestDecl *decl;
    const KestUnitInfo *unit;
    const char *symbol;
    KestType *type;
    const char **names;
    KestType **bindings;
    uint32_t count;
    bool checked;
    // The call that made this copy, and the file it is in. A mistake in a
    // generic body is a mistake in one of its copies, and which one is the
    // call that asked for it.
    KestSpan site;
    const KestSource *site_source;
};

// Resolves declarations, their field types and their signatures, reporting
// what it cannot resolve. Returns false only when the host is out of memory.
bool kest_check(KestArena *arena, KestDiags *diags, const KestUnits *units,
                KestProgram **out);

// Points the program at one file, so what follows resolves names the way that
// file writes them.
void kest_program_in(KestProgram *program, const KestUnitInfo *unit);

// Lookup as a file writes it: its own names bare, everything else prefixed
// with the module it came from.
KestType *kest_lookup_type(KestProgram *program, const char *name,
                           size_t length);
KestSymbol *kest_lookup_global(KestProgram *program, const char *name,
                               size_t length);

// Every function declared under this name, in declaration order. A name with
// one meaning has one; a name with several has several, and which is meant is
// settled by what is passed.
uint32_t kest_overloads(KestProgram *program, const char *name, size_t length,
                        KestSymbol **found, uint32_t room);

// The one declared at this place. Two functions may share a name, so where a
// declaration is is the only thing that names exactly one of them.
KestSymbol *kest_symbol_at(KestProgram *program, const KestSource *source,
                           KestSpan span);

// Whether this name was reached across a module boundary the file did not ask
// to cross. A name found in the file's own module crosses nothing, and so
// does a host receiver, which is a name with a dot in it and not a module.
bool kest_needs_import(KestProgram *program, const char *name, size_t length);

// One copy of a generic struct per set of types, made the first time that set
// is written and found again after that.
KestType *kest_struct_of(KestProgram *program, KestType *shape,
                         KestType **args, uint32_t count);

// Whether a type mentions a name that is standing for itself.
bool kest_mentions_name(const KestType *type);

// The same type with every type name replaced by what it stands for.
KestType *kest_substitute(KestProgram *program, KestType *type,
                          const char **names, KestType **bindings,
                          uint32_t count);

// Works out what each type name has to stand for by putting a declared type
// beside the one that was passed. False when two uses disagree.
bool kest_unify(const KestType *declared, const KestType *given,
                const char **names, KestType **bindings, uint32_t count);

// Binds the type names a generic declaration or instance brought into scope.
// Anything resolved while they are bound sees them and nothing else does.
void kest_bind_types(KestProgram *program, const char **names,
                     KestType **types, uint32_t count);
void kest_unbind_types(KestProgram *program);

// The copy of a generic function for one set of types, made if it is the
// first time that set was asked for.
KestInstance *kest_instance_of(KestProgram *program, const KestDecl *decl,
                               const KestUnitInfo *unit, const char **names,
                               KestType **bindings, uint32_t count);

// Turns a type as written into a resolved type, reporting what it cannot
// resolve.
// The name to write where a type is wanted: its own, without the module in
// front when the module is the file's own, and with its own names for the
// types it takes. Every message that says what to write instead says it this
// way, so none of them can show one type for a shape that takes two.
const char *kest_type_shape(const KestProgram *program, KestArena *arena,
                            const KestType *type);

KestType *kest_resolve_type_ref(KestProgram *program, const KestTypeRef *ref);

// Makes the type of an array holding this element, for a literal whose type
// nobody wrote down.
KestType *kest_array_of(KestProgram *program, KestType *element);
KestType *kest_optional_of(KestProgram *program, KestType *element);
KestType *kest_ref_of(KestProgram *program, KestType *element);
// That many of something, laid out where it stands rather than behind a
// handle. Copying one copies all of it.
KestType *kest_fixed_of(KestProgram *program, KestType *element,
                        uint32_t count);

KestType *kest_find_type(KestProgram *program, const char *name,
                         size_t length);
KestSymbol *kest_find_global(KestProgram *program, const char *name,
                             size_t length);

// The name a program writes for a type, which is the last piece of the one it
// is registered under: a type declared in `examples.flags` is `State` there.
// One place, because a lend matches on it and a message prints it.
const char *kest_type_written(const KestType *type);


// The closest declared name, or NULL when nothing is close enough to be worth
// putting in front of a reader. A wrong suggestion costs more than none.
const char *kest_nearest_global(KestProgram *program, const char *name,
                                size_t length);
const char *kest_nearest_member(const KestType *type, const char *name,
                                size_t length);

// Error types compare equal to everything, so one bad annotation reports once
// rather than at every use of what it annotated.
bool kest_type_equal(const KestType *a, const KestType *b);

// What a constant is worth, worked out from what it is written as: a number, a
// truth or a piece of text, and arithmetic on those and on other constants.
// False when it is not one of those, and then `why` says which of the two ways
// it was not when there is one to name.
//
// One of these, because the compiler pushes the value and a `[T; N]` counts
// with it, and two would be two answers about one constant.
// How many slots it wrote, or nought when it is not one of those. A struct is
// a value laid out flat, so a constant that is one fills a slot per scalar in
// it and the caller says how much room it has.
uint32_t kest_fold_const(KestProgram *program, const KestExpr *expr,
                         KestValue *out, uint32_t room, const char **why);

// Whether a value of this type can be written as text, which is what a hole in
// a string holds and what the command line prints when it calls something.
// Only what has one obvious spelling: a struct has several and the author
// knows which one they meant. `without` names the type that has none, for the
// message. One rule, because the compiler refusing one and the command line
// printing one would be two answers.
bool kest_type_has_text(const KestType *type, const KestType **without);

// Whether anything in this type is a pointer into the machine's own memory:
// a piece of text, a handle to an array or a store, a reference, a function
// value. Those are what a lend cannot carry — the bytes are the host's, and a
// pointer in them is one the machine can neither vouch for nor take back.
// `what` names the one that is, for the message.
bool kest_type_holds_own(const KestType *type, const KestType **what);

// The spelling used in diagnostics: `i32`, `[Player]`, `ref<Npc>?`.
const char *kest_type_name(KestArena *arena, const KestType *type);

// Prints what was resolved, for seeing what the checker built.
// What the program holds, for a person. The file that was named is written out
// in full and what it imported is a line each, because a reader came for the
// one in front of them; `--json` holds all of it either way.
void kest_program_dump(const KestProgram *program, KestArena *arena,
                       const char *root, FILE *out);

// The same, as JSON: what a tool asks when it wants to know what is in a
// program rather than what is wrong with one.
void kest_program_dump_json(const KestProgram *program, KestArena *arena,
                            FILE *out);

#endif
