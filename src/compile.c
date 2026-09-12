#include "compile.h"

#include "check.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

// What there is a most of, in one place, because a number a program can run
// into belongs where somebody can read it and not only where it is enforced.
// Every one of them is a message with the number in it, never a wrap or a
// quiet truncation, and `docs/language.md` says the same numbers.
// How many names a program may ask the host for. An extern is named in the
// instruction that calls it, in two bytes, so the sixty-five-thousand-and-
// thirty-seventh would be called as whichever one that number wraps to: the
// host's, with the program's arguments, and nothing said. See D326.
#define MAX_EXTERNS 65536
_Static_assert(MAX_EXTERNS <= (uint32_t)UINT16_MAX + 1,
               "an extern is named in an instruction in two bytes");

#define MAX_LOCALS 256
#define MAX_LOOPS 16
#define MAX_BREAKS 32
#define MAX_DEFERS 32
// A jump and a loop carry how far as two bytes, so this is how much code there
// can be between one and where it lands.
#define MAX_REACH UINT16_MAX

typedef struct {
    const char *name;
    uint16_t slot;
    // A struct value occupies a run of slots, so a name is a place and a
    // width rather than a single index.
    uint16_t size;
    uint32_t depth;
    // The slot holds where the value is rather than the value. A walk binds
    // its name this way when the body only ever reads fields of it, so
    // reading one field of a wide struct does not copy the rest. See D052.
    bool is_address;
    const KestType *points_at;
} Local;

typedef struct {
    uint32_t start;
    // How many deferred statements were outstanding when the loop opened, so
    // a `break` knows which of them it is leaving.
    uint16_t deferred;
    uint32_t breaks[MAX_BREAKS];
    uint32_t break_count;
    // `continue` jumps forward to a pad placed after the body, because in a
    // `for` the step comes after the body and jumping to the top would skip
    // it.
    uint32_t continues[MAX_BREAKS];
    uint32_t continue_count;
} Loop;

typedef struct {
    KestProgram *program;
    KestModule *module;
    KestChunk *chunk;
    // The files, so a name that is not a local can be looked up rather than
    // becoming a load from somewhere.
    const KestUnits *units;

    Local locals[MAX_LOCALS];
    uint16_t local_count;
    uint16_t next_slot;
    uint16_t slot_high_water;
    uint32_t depth;

    Loop loops[MAX_LOOPS];
    uint32_t loop_count;
    uint32_t unit;

    // The last two instructions written and where each starts, so that a jump
    // can take the `not` before it and the comparison before that into
    // itself. Two, because that is as far back as anything reaches.
    uint8_t last_op;
    uint32_t last_at;
    uint8_t before_op;
    uint32_t before_at;

    // What has been deferred and not yet run, innermost last. A block runs
    // what it added when it ends; a `return` runs everything; a `break` runs
    // what the loop it is leaving added. See D061.
    const KestExpr *deferred[MAX_DEFERS];
    uint16_t defer_count;

    // Compiling an expression always leaves one value behind and compiling a
    // statement leaves none, so following the emit sites gives the exact
    // depth rather than a bound.
    uint16_t stack_depth;
    uint16_t stack_high_water;

    // A reported problem does not stop the walk: D008 wants one run to report
    // the whole file. Only running out of memory stops it, because after that
    // nothing further is true.
    bool failed;
    bool out_of_memory;
} Compiler;

static void refuse(Compiler *compiler, KestSpan span, const char *code,
                   const char *format, ...) {
    va_list args;
    va_start(args, format);
    kest_diags_addv(compiler->program->diags, KEST_SEVERITY_ERROR, code, span,
                    format, args);
    va_end(args);
    compiler->failed = true;
}

// What the checker allowed and this cannot emit. Reaching one of these means
// the two halves of this compiler disagree about what a program is, which is
// this project's mistake and not the program's — so it says so, in the words
// the emitted-code proof uses for the same kind of news. A diagnostic rather
// than an assert, because a program that trips it should be told rather than
// stopped.
static void fault(Compiler *compiler, KestSpan span, const char *what) {
    refuse(compiler, span, "K0505", "%s, which the checker allowed", what);
    kest_diags_fault(compiler->program->diags,
                     "the two halves of the compiler disagree about what a "
                     "program is");
}

static void stack_push(Compiler *compiler, uint16_t count) {
    compiler->stack_depth += count;
    if (compiler->stack_depth > compiler->stack_high_water) {
        compiler->stack_high_water = compiler->stack_depth;
    }
}

static void stack_pop(Compiler *compiler, uint16_t count) {
    compiler->stack_depth =
        compiler->stack_depth >= count ? compiler->stack_depth - count : 0;
}

static void emit(Compiler *compiler, uint8_t byte, KestSpan origin) {
    // Where the last instruction started, which is what lets the jump that
    // reads a comparison take the comparison with it. Only opcodes come
    // through here; the numbers after them go through `emit_u16`.
    compiler->before_op = compiler->last_op;
    compiler->before_at = compiler->last_at;
    compiler->last_op = byte;
    compiler->last_at = compiler->chunk->code_count;
    if (!kest_chunk_emit(compiler->module, compiler->chunk, byte,
                         origin.offset)) {
        compiler->out_of_memory = true;
    }
}

static void emit_u16(Compiler *compiler, uint16_t value, KestSpan origin) {
    if (!kest_chunk_emit_u16(compiler->module, compiler->chunk, value,
                             origin.offset)) {
        compiler->out_of_memory = true;
    }
}

static void emit_constant(Compiler *compiler, KestValue value,
                          KestConstClass class, KestSpan origin) {
    uint32_t index =
        kest_chunk_constant(compiler->module, compiler->chunk, value, class);
    stack_push(compiler, 1);
    emit(compiler, KEST_OP_CONST, origin);
    emit_u16(compiler, (uint16_t)index, origin);
}

// Writes a jump with a placeholder distance and returns where the placeholder
// is, because how far it goes is not known until the body has been emitted.
// The comparison a jump reads, when the jump is the next thing after it. Every
// one of these leaves its answer on the stack for one instruction, which then
// pops it and throws it away, so the pair is one instruction and one dispatch.
// Only whole numbers: they are what a loop counts with and what an index is.
static uint8_t fused_with_jump(uint8_t compare, bool asking_true) {
    switch (compare) {
    case KEST_OP_LT_I:
        return asking_true ? KEST_OP_JUMP_TRUE_LT_I : KEST_OP_JUMP_FALSE_LT_I;
    case KEST_OP_LE_I:
        return asking_true ? KEST_OP_JUMP_TRUE_LE_I : KEST_OP_JUMP_FALSE_LE_I;
    case KEST_OP_GT_I:
        return asking_true ? KEST_OP_JUMP_TRUE_GT_I : KEST_OP_JUMP_FALSE_GT_I;
    case KEST_OP_GE_I:
        return asking_true ? KEST_OP_JUMP_TRUE_GE_I : KEST_OP_JUMP_FALSE_GE_I;
    case KEST_OP_EQ_I:
        return asking_true ? KEST_OP_JUMP_TRUE_EQ_I : KEST_OP_JUMP_FALSE_EQ_I;
    case KEST_OP_NE_I:
        return asking_true ? KEST_OP_JUMP_TRUE_NE_I : KEST_OP_JUMP_FALSE_NE_I;
    case KEST_OP_LT_F:
        return asking_true ? KEST_OP_JUMP_TRUE_LT_F : KEST_OP_JUMP_FALSE_LT_F;
    case KEST_OP_LE_F:
        return asking_true ? KEST_OP_JUMP_TRUE_LE_F : KEST_OP_JUMP_FALSE_LE_F;
    case KEST_OP_GT_F:
        return asking_true ? KEST_OP_JUMP_TRUE_GT_F : KEST_OP_JUMP_FALSE_GT_F;
    case KEST_OP_GE_F:
        return asking_true ? KEST_OP_JUMP_TRUE_GE_F : KEST_OP_JUMP_FALSE_GE_F;
    case KEST_OP_EQ_F:
        return asking_true ? KEST_OP_JUMP_TRUE_EQ_F : KEST_OP_JUMP_FALSE_EQ_F;
    case KEST_OP_NE_F:
        return asking_true ? KEST_OP_JUMP_TRUE_NE_F : KEST_OP_JUMP_FALSE_NE_F;
    default:
        return asking_true ? KEST_OP_JUMP_TRUE : KEST_OP_JUMP_FALSE;
    }
}

static uint32_t emit_jump(Compiler *compiler, uint8_t op, KestSpan origin) {
    // A comparison is one byte and carries nothing after it, so it is the
    // last instruction when it is the last byte. Taking it back here rather
    // than looking for pairs afterwards means nothing has been written that
    // could point at the byte being taken away.
    // Twice at most: the jump takes back the `not` before it, and then the
    // comparison that `not` was turning round. Both are one byte and both
    // came through `emit`, which is what makes "the last instruction" a thing
    // that can be known rather than guessed at from the bytes.
    for (uint32_t round = 0; round < 2; round++) {
        if ((op != KEST_OP_JUMP_FALSE && op != KEST_OP_JUMP_TRUE) ||
            compiler->last_at + 1 != compiler->chunk->code_count) {
            break;
        }
        bool asking_true = op == KEST_OP_JUMP_TRUE;
        uint8_t fused =
            compiler->last_op == KEST_OP_NOT
                ? (asking_true ? KEST_OP_JUMP_FALSE : KEST_OP_JUMP_TRUE)
                : fused_with_jump(compiler->last_op, asking_true);
        if (fused == op) {
            break;
        }
        compiler->chunk->code_count = compiler->last_at;
        op = fused;
        compiler->last_op = compiler->before_op;
        compiler->last_at = compiler->before_at;
    }
    emit(compiler, op, origin);
    emit_u16(compiler, 0, origin);
    return compiler->chunk->code_count - 2;
}

static void patch_jump(Compiler *compiler, uint32_t placeholder,
                       KestSpan origin) {
    uint32_t distance = compiler->chunk->code_count - placeholder - 2;
    if (distance > MAX_REACH) {
        refuse(compiler, origin, "K0503",
               "this jumps %u bytes of code, and a jump reaches %u", distance,
               (uint32_t)MAX_REACH);
        return;
    }
    compiler->chunk->code[placeholder] = (uint8_t)(distance & 0xff);
    compiler->chunk->code[placeholder + 1] = (uint8_t)(distance >> 8);
}

// Where the jumps that leave a condition are, so that whatever the condition
// is in can send all of them to the same place. There is one per `&&` and
// `||` in it and one at the end, and no most: a condition is written as long
// as somebody writes it, and a number here would be a number that changes what
// is emitted without refusing anything, which is the one kind nobody can see.
typedef struct {
    uint32_t *at;
    uint32_t count;
    uint32_t capacity;
} Exits;

static void take_exit(Compiler *compiler, Exits *exits, uint32_t at) {
    if (exits->count == exits->capacity) {
        uint32_t grown = exits->capacity == 0 ? 8 : exits->capacity * 2;
        uint32_t *moved =
            KEST_ARENA_ARRAY(compiler->program->arena, uint32_t, grown);
        if (moved == NULL) {
            compiler->out_of_memory = true;
            return;
        }
        if (exits->count > 0) {
            memcpy(moved, exits->at, sizeof(uint32_t) * exits->count);
        }
        exits->at = moved;
        exits->capacity = grown;
    }
    exits->at[exits->count++] = at;
}

static Exits one_exit(Compiler *compiler, uint32_t at) {
    Exits exits = {NULL, 0, 0};
    take_exit(compiler, &exits, at);
    return exits;
}

static void patch_exits(Compiler *compiler, const Exits *exits,
                        KestSpan origin) {
    for (uint32_t i = 0; i < exits->count; i++) {
        patch_jump(compiler, exits->at[i], origin);
    }
}

static void emit_loop(Compiler *compiler, uint32_t start, KestSpan origin) {
    emit(compiler, KEST_OP_LOOP, origin);
    uint32_t distance = compiler->chunk->code_count + 2 - start;
    if (distance > MAX_REACH) {
        refuse(compiler, origin, "K0503",
               "this loop is %u bytes of code, and a loop reaches back %u",
               distance, (uint32_t)MAX_REACH);
        distance = 0;
    }
    emit_u16(compiler, (uint16_t)distance, origin);
}

static const char *span_text(Compiler *compiler, KestSpan span) {
    return compiler->program->source->text + span.offset;
}

static Local *find_local(Compiler *compiler, KestSpan span) {
    const char *name = span_text(compiler, span);
    for (uint16_t i = compiler->local_count; i > 0; i--) {
        Local *local = &compiler->locals[i - 1];
        if (strlen(local->name) == span.length &&
            memcmp(local->name, name, span.length) == 0) {
            return local;
        }
    }
    return NULL;
}

// Locals are a stack, so a slot is the position and a scope is dropped by
// rewinding the count. The high water mark is the frame size.
static uint16_t type_slots(const KestType *type) {
    return type == NULL || type->slots == 0 ? 1 : type->slots;
}

// A slot with no name, for what `for` needs to keep between iterations.
static uint16_t reserve_slot(Compiler *compiler, uint16_t size) {
    uint16_t slot = compiler->next_slot;
    compiler->next_slot += size;
    if (compiler->next_slot > compiler->slot_high_water) {
        compiler->slot_high_water = compiler->next_slot;
    }
    return slot;
}

// A name for slots that are already somewhere, which is what a match arm
// gives what the case it answered was carrying.
static void bind_local(Compiler *compiler, KestSpan span, uint16_t slot,
                       uint16_t size) {
    if (compiler->local_count == MAX_LOCALS) {
        refuse(compiler, span, "K0502", "a function holds at most %d names",
               MAX_LOCALS);
        return;
    }
    Local *local = &compiler->locals[compiler->local_count++];
    memset(local, 0, sizeof *local);
    local->name = kest_arena_strndup(compiler->program->arena,
                                     span_text(compiler, span), span.length);
    if (local->name == NULL) {
        // A local is found by its name, so one without a name is a slot
        // nothing can reach and a pointer everything that looks for it would
        // read. See D510.
        compiler->out_of_memory = true;
        return;
    }
    local->slot = slot;
    local->size = size;
    local->depth = compiler->depth;
}

static uint16_t declare_local(Compiler *compiler, KestSpan span,
                              const KestType *type) {
    if (compiler->local_count == MAX_LOCALS) {
        refuse(compiler, span, "K0502", "a function holds at most %d names",
               MAX_LOCALS);
        return 0;
    }
    // Slots are reused between scopes and between functions, so everything a
    // name holds is written here rather than left over from the last one.
    Local *local = &compiler->locals[compiler->local_count++];
    memset(local, 0, sizeof *local);
    local->name = kest_arena_strndup(compiler->program->arena,
                                     span_text(compiler, span), span.length);
    if (local->name == NULL) {
        compiler->out_of_memory = true;
        return 0;
    }
    local->slot = compiler->next_slot;
    local->size = type_slots(type);
    local->depth = compiler->depth;

    compiler->next_slot += local->size;
    if (compiler->next_slot > compiler->slot_high_water) {
        compiler->slot_high_water = compiler->next_slot;
    }
    return local->slot;
}

// How many slots a value of this type occupies on the stack. Nothing is zero
// except a call that returns nothing.
static uint16_t value_slots(const KestType *type) {
    if (type == NULL || type->tag == KEST_T_VOID) {
        return 0;
    }
    return type->slots == 0 ? 1 : type->slots;
}

// Where the module keeps this type's memory layout, which is what an array
// element is and what a write through an address writes.
static uint16_t layout_of(Compiler *compiler, const KestType *type) {
    int32_t index = kest_module_layout(compiler->module, type);
    if (index < 0) {
        compiler->out_of_memory = true;
        return 0;
    }
    return (uint16_t)index;
}

static void emit_load(Compiler *compiler, uint16_t slot, uint16_t size,
                      KestSpan origin) {
    emit(compiler, size == 1 ? KEST_OP_LOAD : KEST_OP_LOADN, origin);
    emit_u16(compiler, slot, origin);
    if (size != 1) {
        emit_u16(compiler, size, origin);
    }
}

static void emit_store(Compiler *compiler, uint16_t slot, uint16_t size,
                       KestSpan origin) {
    emit(compiler, size == 1 ? KEST_OP_STORE : KEST_OP_STOREN, origin);
    emit_u16(compiler, slot, origin);
    if (size != 1) {
        emit_u16(compiler, size, origin);
    }
}

static const KestMember *find_member(const KestType *type, const char *name,
                                     size_t length) {
    if (type == NULL || type->tag != KEST_T_STRUCT) {
        return NULL;
    }
    for (uint32_t i = 0; i < type->member_count; i++) {
        if (strlen(type->members[i].name) == length &&
            memcmp(type->members[i].name, name, length) == 0) {
            return &type->members[i];
        }
    }
    return NULL;
}

// A place is a run of slots that a name reaches by arithmetic. `v` is one and
// so is `v.a.b`, because a struct is laid out flat, so reading a field costs
// an addition rather than a load.
// An index written down, or -1 when it was not. How many of them is known, so
// one of them at a written place is a slot like a field is.
static int64_t written_index(Compiler *compiler, const KestExpr *expr) {
    if (expr->kind != KEST_EXPR_INT) {
        return -1;
    }
    const char *digits = span_text(compiler, expr->span);
    int64_t at = 0;
    for (uint32_t i = 0; i < expr->span.length; i++) {
        at = at * 10 + (digits[i] - '0');
        if (at > 65535) {
            return -1;
        }
    }
    return at;
}

static bool resolve_place(Compiler *compiler, const KestExpr *expr,
                          uint16_t *slot, uint16_t *size) {
    // One of that many, at a place written down, is where the run is plus how
    // far in: the same arithmetic a field of a struct is.
    if (expr->kind == KEST_EXPR_INDEX && expr->index.object->type != NULL &&
        expr->index.object->type->tag == KEST_T_FIXED) {
        const KestType *run = expr->index.object->type;
        int64_t at = written_index(compiler, expr->index.index);
        uint16_t base = 0;
        uint16_t run_size = 0;
        if (at >= 0 && (uint64_t)at < run->count &&
            resolve_place(compiler, expr->index.object, &base, &run_size)) {
            uint16_t stride = value_slots(run->element);
            *slot = (uint16_t)(base + at * stride);
            *size = stride;
            return true;
        }
        return false;
    }
    if (expr->kind == KEST_EXPR_NAME) {
        Local *local = find_local(compiler, expr->span);
        if (local == NULL || local->is_address) {
            return false;
        }
        *slot = local->slot;
        *size = local->size;
        return true;
    }
    if (expr->kind != KEST_EXPR_FIELD) {
        return false;
    }

    uint16_t base = 0;
    uint16_t base_size = 0;
    if (!resolve_place(compiler, expr->field.object, &base, &base_size)) {
        return false;
    }
    const KestMember *member =
        find_member(expr->field.object->type,
                    span_text(compiler, expr->field.name),
                    expr->field.name.length);
    if (member == NULL) {
        return false;
    }
    *slot = base + member->offset;
    *size = value_slots(member->type);
    return true;
}



static bool is_float(const KestType *type) {
    return type != NULL && type->tag == KEST_T_FLOAT;
}

// `f32` and `f64` are different instructions, because rounding to the narrower
// one is part of what the type means.
static bool is_narrow(const KestType *type) {
    return type != NULL && type->tag == KEST_T_FLOAT && type->width == 32;
}

// Which instruction compares, by what is being compared. One row an operator,
// because the question is the same four every time — a piece of text, a float,
// an unsigned number, or the plain one — and it was written out six times.
// `==` and `!=` on an enum are the exception and are answered where they are
// emitted: both sides are a run of slots rather than one.
static const struct {
    KestTokenKind op;
    uint8_t whole;
    uint8_t without_sign;
    uint8_t real;
    uint8_t text;
} COMPARISONS[] = {
    {KEST_TOK_LT, KEST_OP_LT_I, KEST_OP_LT_U, KEST_OP_LT_F, KEST_OP_LT_T},
    {KEST_TOK_LTEQ, KEST_OP_LE_I, KEST_OP_LE_U, KEST_OP_LE_F, KEST_OP_LE_T},
    {KEST_TOK_GT, KEST_OP_GT_I, KEST_OP_GT_U, KEST_OP_GT_F, KEST_OP_GT_T},
    {KEST_TOK_GTEQ, KEST_OP_GE_I, KEST_OP_GE_U, KEST_OP_GE_F, KEST_OP_GE_T},
    // Equality does not ask whether a number has a sign: the same bits are
    // the same bits either way.
    {KEST_TOK_EQEQ, KEST_OP_EQ_I, KEST_OP_EQ_I, KEST_OP_EQ_F, KEST_OP_EQ_T},
    {KEST_TOK_BANGEQ, KEST_OP_NE_I, KEST_OP_NE_I, KEST_OP_NE_F, KEST_OP_NE_T},
};

// The row for an operator that compares, or NULL for one that does not.
static const uint8_t *compares(KestTokenKind op, bool text, bool real,
                               bool without_sign) {
    for (size_t i = 0; i < sizeof(COMPARISONS) / sizeof(COMPARISONS[0]); i++) {
        if (COMPARISONS[i].op != op) {
            continue;
        }
        return text ? &COMPARISONS[i].text
                    : real ? &COMPARISONS[i].real
                           : (without_sign ? &COMPARISONS[i].without_sign
                                           : &COMPARISONS[i].whole);
    }
    return NULL;
}

// A result wider than its type is not the answer the type describes, so it is
// cut back. Sixty-four bits is the slot, so nothing is cut there.
static void emit_narrow(Compiler *compiler, const KestType *type,
                        KestSpan span) {
    if (type == NULL || type->width == 64 ||
        (type->tag != KEST_T_INT && type->tag != KEST_T_FLAGS)) {
        return;
    }
    emit(compiler, KEST_OP_NARROW, span);
    emit_u16(compiler, kest_scalar_of(type), span);
}

static bool is_unsigned(const KestType *type) {
    return type != NULL && type->tag == KEST_T_INT && !type->is_signed;
}

// Copies the content of a string, resolving escapes. The span is the
// characters between the quotes, or one run of them when the string was
// written with holes in it.
// What a string literal holds, and what a number literal is worth. Both are
// the lexer's to know, because both are about how a thing is spelled.
static const char *literal_text(Compiler *compiler, KestSpan span) {
    return kest_literal_text(compiler->program->arena, compiler->program->source,
                             span);
}

static double parse_real(Compiler *compiler, KestSpan span) {
    return kest_literal_real(compiler->program->source, span);
}

static void compile_expr(Compiler *compiler, const KestExpr *expr);
static void compile_block(Compiler *compiler, const KestBlock *block);
static void run_deferred(Compiler *compiler, uint16_t from, KestSpan span);
static void bind_local(Compiler *compiler, KestSpan span, uint16_t slot,
                       uint16_t size);
static uint16_t reserve_slot(Compiler *compiler, uint16_t size);

// A constant is written into every use of it rather than loaded, which is
// what makes it a constant rather than a variable nobody assigns to.
// A function named where a value is wanted is which function it is. The
// checker settled which one, and its symbol is what it was compiled under, so
// nothing is chosen twice.
static bool compile_function_value(Compiler *compiler, const KestExpr *expr) {
    if (expr->type == NULL || expr->type->tag != KEST_T_FN ||
        expr->type->symbol == NULL) {
        return false;
    }
    int32_t index = kest_module_find(compiler->module, expr->type->symbol);
    if (index < 0) {
        fault(compiler, expr->span, "this names a function that was never "
                                    "compiled");
        return true;
    }
    KestValue which = {0};
    which.integer = index;
    stack_push(compiler, 1);
    emit_constant(compiler, which, KEST_CONST_INT, expr->span);
    return true;
}

// A value is laid out flat, so a constant that is a struct is a push a scalar,
// each with what its bits mean beside it: the machine never reads that and the
// disassembler does.
static void value_classes(const KestType *type, uint8_t *classes,
                          uint32_t *at) {
    if (type != NULL && type->tag == KEST_T_STRUCT) {
        for (uint32_t i = 0; i < type->member_count; i++) {
            value_classes(type->members[i].type, classes, at);
        }
        return;
    }
    if (type != NULL && type->tag == KEST_T_FIXED) {
        for (uint32_t i = 0; i < type->count; i++) {
            value_classes(type->element, classes, at);
        }
        return;
    }
    classes[(*at)++] =
        type != NULL && type->tag == KEST_T_FLOAT
            ? KEST_CONST_FLOAT
            : (type != NULL && type->tag == KEST_T_TEXT ? KEST_CONST_TEXT
                                                        : KEST_CONST_INT);
}

// The run put in the chunk beside the code, with what each of its slots means
// beside it, and where it starts.
static bool constant_run(Compiler *compiler, const KestType *type,
                         const KestValue *values, uint16_t slots,
                         uint32_t *first) {
    // Worked out into this rather than into a block of the arena, which is
    // what D676 did for the values these describe and did not do for the
    // description: `kest_chunk_constant_run` copies what it is handed into the
    // chunk, so nothing here outlives the call, and a block taken for it is a
    // block nobody reads and nothing gives back. Sixteen for the same reason
    // the values take sixteen, which is that a run wider than that is a table
    // rather than a value. See D754.
    // Nought to start with, because that is what a block of the arena arrives
    // as and what `value_classes` leaves the slots it does not reach: a
    // description read past what was written into it is a constant whose kind
    // is whatever the stack held, and two builds of one program would mark
    // differently.
    uint8_t held[16] = {0};
    uint8_t *classes = held;
    if (slots > (uint16_t)(sizeof(held) / sizeof(held[0]))) {
        classes = KEST_ARENA_ARRAY(compiler->program->arena, uint8_t, slots);
        if (classes == NULL) {
            compiler->out_of_memory = true;
            return false;
        }
    }
    uint32_t at = 0;
    value_classes(type, classes, &at);
    *first = kest_chunk_constant_run(compiler->module, compiler->chunk, values,
                                     classes, slots);
    return true;
}

static void emit_value_slots(Compiler *compiler, const KestType *type,
                             const KestValue *values, uint16_t slots,
                             KestSpan span) {
    // One of them is one push. A run of them is one instruction and one copy,
    // because a table of sixty-four numbers should not cost sixty-four
    // instructions every time it is read.
    uint32_t first = 0;
    if (!constant_run(compiler, type, values, slots, &first)) {
        return;
    }
    if (slots == 1) {
        stack_push(compiler, 1);
        emit(compiler, KEST_OP_CONST, span);
        emit_u16(compiler, (uint16_t)first, span);
        return;
    }
    stack_push(compiler, slots);
    emit(compiler, KEST_OP_CONST_RUN, span);
    emit_u16(compiler, (uint16_t)first, span);
    emit_u16(compiler, slots, span);
}

// Anything that is a constant when it is written down: a name, a field of one,
// an element of one. Nothing is copied into slots to be read back out.
static bool compile_folded(Compiler *compiler, const KestExpr *expr) {
    uint16_t slots = value_slots(expr->type);
    if (expr->type == NULL || slots == 0) {
        return false;
    }
    // A name that is a constant was worked out where it was declared, so this
    // reads what came of that rather than working it out again. Without it a
    // wide constant read forty times was folded forty times, which the count
    // of folds says out loud. See D675.
    if (expr->kind == KEST_EXPR_NAME || expr->kind == KEST_EXPR_FIELD) {
        const KestSymbol *named = kest_lookup_global(
            compiler->program, span_text(compiler, expr->span),
            expr->span.length);
        if (named != NULL && named->is_const && named->folded != NULL &&
            named->folded_slots == slots) {
            emit_value_slots(compiler, expr->type, named->folded, slots,
                             expr->span);
            return true;
        }
    }
    // Worked out into this rather than into a block of the arena. Most of what
    // reaches here is not a constant at all — ninety-one askings to nineteen
    // answers in one example — and a block taken for each of those is a block
    // nobody reads and nothing gives back. What comes of a fold is written
    // into the chunk, so nothing needs to outlive this. See D676.
    KestValue held[16];
    KestValue *values = held;
    if (slots > (uint16_t)(sizeof(held) / sizeof(held[0]))) {
        values = KEST_ARENA_ARRAY(compiler->program->arena, KestValue, slots);
        if (values == NULL) {
            return false;
        }
    }
    const char *why = NULL;
    if (kest_fold_const(compiler->program, expr, values, slots, &why,
                        NULL) != slots) {
        return false;
    }
    if (compiler->chunk != NULL) {
        compiler->chunk->folded++;
        compiler->chunk->folded_slots += slots;
    }
    emit_value_slots(compiler, expr->type, values, slots, expr->span);
    return true;
}

static void compile_constant(Compiler *compiler, const KestExpr *expr) {
    const char *name = span_text(compiler, expr->span);

    if (compile_function_value(compiler, expr)) {
        return;
    }

    const KestSymbol *symbol =
        kest_lookup_global(compiler->program, name, expr->span.length);
    // Worked out where it was declared, so every use of it reads what is
    // already there: a constant read five times was folded five times before
    // D674, and what was wrong with one was said as many times as it was read.
    if (symbol != NULL && symbol->is_const && symbol->folded != NULL) {
        emit_value_slots(compiler, symbol->type, symbol->folded,
                         (uint16_t)symbol->folded_slots, expr->span);
        return;
    }
    if (symbol != NULL && symbol->is_const && symbol->would_not_fold) {
        // Said once, at the declaration. A use of a constant that could not be
        // worked out is not a second thing wrong with the program.
        return;
    }
    if (symbol != NULL && symbol->is_const) {
        uint16_t slots = value_slots(symbol->type);
        KestValue *values =
            KEST_ARENA_ARRAY(compiler->program->arena, KestValue,
                             slots == 0 ? 1 : slots);
        const char *why = NULL;
        if (values == NULL) {
            compiler->out_of_memory = true;
            return;
        }
        // Worked out in the file it was written in. What a literal is worth is
        // read out of the source at the span it stands at, and a constant from
        // another module has spans into that module's file — read against this
        // one they name whatever bytes happen to be at those offsets, which is
        // a number nobody wrote. See D665.
        const KestSource *reading = compiler->program->source;
        if (symbol->source != NULL) {
            compiler->program->source = symbol->source;
        }
        bool never = false;
        uint32_t filled = kest_fold_const(compiler->program, symbol->value,
                                          values, slots, &why, &never);
        compiler->program->source = reading;
        if (filled != slots) {
            // Two refusals rather than one. A constant made of itself or
            // divided by nought is a mistake in what was written, and one that
            // asks for a choice or a call is a rule of this language — the
            // first is fixed where it is and the second is written another way
            // altogether. A reader is told either way; a tool sorting refusals
            // could not tell them apart while both were `K0504`. See D673.
            if (never) {
                refuse(compiler, expr->span, "K0510",
                       "`%.*s` is made while running, so it is not a constant",
                       (int)expr->span.length, name);
            } else {
                refuse(compiler, expr->span, "K0504",
                       "`%.*s` is not worked out where it is written",
                       (int)expr->span.length, name);
            }
            kest_diags_suggest(compiler->program->diags, "%s",
                               why != NULL
                                   ? why
                                   : "a constant is a number, a truth or a "
                                     "piece of text, and arithmetic on those "
                                     "and on other constants");
            return;
        }
        emit_value_slots(compiler, symbol->type, values, slots, expr->span);
        return;
    }

    fault(compiler, expr->span,
          "this is a name that is not a local, a constant or a function");
}

// Whether a name is used for nothing but reading fields of it. A walk binds
// its name to where the element is when that holds, so a body that wants one
// field of a wide struct does not copy the rest of it.
//
// It is the whole body or nothing: one use of the name on its own — passed,
// returned, compared, assigned to — and the name has to be a value.
static bool reads_only_fields(Compiler *compiler, const KestBlock *block,
                              const char *name, size_t length,
                              bool fields_are_fine);

static bool name_is(Compiler *compiler, const KestExpr *expr, const char *name,
                    size_t length) {
    return expr != NULL && expr->kind == KEST_EXPR_NAME &&
           expr->span.length == length &&
           memcmp(span_text(compiler, expr->span), name, length) == 0;
}

static bool expr_reads_only_fields(Compiler *compiler, const KestExpr *expr,
                                   const char *name, size_t length,
                                   bool fields_are_fine) {
    if (expr == NULL) {
        return true;
    }
    if (name_is(compiler, expr, name, length)) {
        return false;
    }
    switch (expr->kind) {
    case KEST_EXPR_FIELD:
        // The one shape that is allowed: the name, and a field of it. Asked
        // the other way, with `fields_are_fine` off, no shape is allowed and
        // the answer is whether the name is mentioned at all.
        if (name_is(compiler, expr->field.object, name, length)) {
            return fields_are_fine;
        }
        return expr_reads_only_fields(compiler, expr->field.object, name, length,
                                      fields_are_fine);
    case KEST_EXPR_UNARY:
        return expr_reads_only_fields(compiler, expr->unary.operand, name, length,
                                      fields_are_fine);
    case KEST_EXPR_BINARY:
        return expr_reads_only_fields(compiler, expr->binary.left, name, length,
                                      fields_are_fine) &&
               expr_reads_only_fields(compiler, expr->binary.right, name, length,
                                      fields_are_fine);
    case KEST_EXPR_CALL:
        if (!expr_reads_only_fields(compiler, expr->call.callee, name, length,
                                      fields_are_fine)) {
            return false;
        }
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            if (!expr_reads_only_fields(compiler, expr->call.args[i], name, length,
                                      fields_are_fine)) {
                return false;
            }
        }
        return true;
    case KEST_EXPR_INDEX:
        return expr_reads_only_fields(compiler, expr->index.object, name, length,
                                      fields_are_fine) &&
               expr_reads_only_fields(compiler, expr->index.index, name, length,
                                      fields_are_fine);
    case KEST_EXPR_ARRAY:
        for (uint32_t i = 0; i < expr->array.count; i++) {
            if (!expr_reads_only_fields(compiler, expr->array.items[i], name, length,
                                      fields_are_fine)) {
                return false;
            }
        }
        return true;
    case KEST_EXPR_TEXT:
        for (uint32_t i = 0; i < expr->text.count; i++) {
            if (!expr_reads_only_fields(compiler, expr->text.parts[i].value, name, length,
                                      fields_are_fine)) {
                return false;
            }
        }
        return true;
    case KEST_EXPR_MATCH: {
        for (uint32_t i = 0; i < expr->choose.subject_count; i++) {
            if (!expr_reads_only_fields(compiler, expr->choose.subjects[i], name, length,
                                      fields_are_fine)) {
                return false;
            }
        }
        for (uint32_t a = 0; a < expr->choose.arm_count; a++) {
            const KestArm *arm = &expr->choose.arms[a];
            if (!expr_reads_only_fields(compiler, arm->value, name, length,
                                      fields_are_fine) ||
                !reads_only_fields(compiler, &arm->body, name, length,
                                      fields_are_fine)) {
                return false;
            }
        }
        return true;
    }
    case KEST_EXPR_IF: {
        const KestBranch *branch = expr->branch;
        return expr_reads_only_fields(compiler, branch->condition, name, length,
                                      fields_are_fine) &&
               expr_reads_only_fields(compiler, branch->then_value, name, length,
                                      fields_are_fine) &&
               reads_only_fields(compiler, &branch->then_body, name, length,
                                      fields_are_fine) &&
               expr_reads_only_fields(compiler, branch->otherwise, name, length,
                                      fields_are_fine) &&
               expr_reads_only_fields(compiler, branch->else_value, name, length,
                                      fields_are_fine) &&
               reads_only_fields(compiler, &branch->else_body, name, length,
                                      fields_are_fine);
    }
    default:
        return true;
    }
}

static bool reads_only_fields(Compiler *compiler, const KestBlock *block,
                              const char *name, size_t length,
                              bool fields_are_fine) {
    for (uint32_t i = 0; i < block->count; i++) {
        const KestStmt *stmt = block->items[i];
        switch (stmt->kind) {
        case KEST_STMT_LET:
            // A name declared over the top of it makes what follows about
            // something else, which this does not try to tell apart.
            if (stmt->let.name.length == length &&
                memcmp(span_text(compiler, stmt->let.name), name, length) ==
                    0) {
                return false;
            }
            if (!expr_reads_only_fields(compiler, stmt->let.value, name, length,
                                      fields_are_fine)) {
                return false;
            }
            break;
        case KEST_STMT_ASSIGN:
            // Writing a field of it is writing, not reading.
            if (stmt->assign.target != NULL &&
                stmt->assign.target->kind == KEST_EXPR_FIELD &&
                name_is(compiler, stmt->assign.target->field.object, name,
                        length)) {
                return false;
            }
            if (!expr_reads_only_fields(compiler, stmt->assign.target, name, length,
                                      fields_are_fine) ||
                !expr_reads_only_fields(compiler, stmt->assign.value, name, length,
                                      fields_are_fine)) {
                return false;
            }
            break;
        case KEST_STMT_EXPR:
            if (!expr_reads_only_fields(compiler, stmt->value, name, length,
                                      fields_are_fine)) {
                return false;
            }
            break;
        case KEST_STMT_WHILE:
            if (!expr_reads_only_fields(compiler, stmt->loop.condition, name, length,
                                      fields_are_fine) ||
                !reads_only_fields(compiler, &stmt->loop.body, name, length,
                                      fields_are_fine)) {
                return false;
            }
            break;
        case KEST_STMT_FOR:
            if (!expr_reads_only_fields(compiler, stmt->each.sequence, name, length,
                                      fields_are_fine) ||
                !expr_reads_only_fields(compiler, stmt->each.until, name, length,
                                      fields_are_fine) ||
                !reads_only_fields(compiler, &stmt->each.body, name, length,
                                      fields_are_fine)) {
                return false;
            }
            break;
        case KEST_STMT_RETURN:
            if (!expr_reads_only_fields(compiler, stmt->result, name, length,
                                      fields_are_fine)) {
                return false;
            }
            break;
        case KEST_STMT_BLOCK:
            if (!reads_only_fields(compiler, &stmt->block, name, length,
                                 fields_are_fine)) {
                return false;
            }
            break;
        default:
            break;
        }
    }
    return true;
}

// Whether anything in here could write into an array. There is no global
// mutable state in this language (D002's rule for the implementation is the
// language's rule too), so a write reaches an array only through a name in
// scope or through a call that was handed one. Both are refused rather than
// told apart, because telling two handles apart is a question this compiler
// does not ask.
static bool writes_no_arrays(Compiler *compiler, const KestBlock *block);

static bool expr_writes_no_arrays(Compiler *compiler, const KestExpr *expr) {
    if (expr == NULL) {
        return true;
    }
    switch (expr->kind) {
    case KEST_EXPR_CALL:
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            const KestType *given = expr->call.args[i]->type;
            if (given != NULL && (given->tag == KEST_T_ARRAY ||
                                  given->tag == KEST_T_STORE ||
                                  given->tag == KEST_T_REF)) {
                return false;
            }
            if (!expr_writes_no_arrays(compiler, expr->call.args[i])) {
                return false;
            }
        }
        return expr_writes_no_arrays(compiler, expr->call.callee);
    case KEST_EXPR_UNARY:
        return expr_writes_no_arrays(compiler, expr->unary.operand);
    case KEST_EXPR_BINARY:
        return expr_writes_no_arrays(compiler, expr->binary.left) &&
               expr_writes_no_arrays(compiler, expr->binary.right);
    case KEST_EXPR_FIELD:
        return expr_writes_no_arrays(compiler, expr->field.object);
    case KEST_EXPR_INDEX:
        return expr_writes_no_arrays(compiler, expr->index.object) &&
               expr_writes_no_arrays(compiler, expr->index.index);
    case KEST_EXPR_ARRAY:
        for (uint32_t i = 0; i < expr->array.count; i++) {
            if (!expr_writes_no_arrays(compiler, expr->array.items[i])) {
                return false;
            }
        }
        return true;
    case KEST_EXPR_TEXT:
        for (uint32_t i = 0; i < expr->text.count; i++) {
            if (!expr_writes_no_arrays(compiler, expr->text.parts[i].value)) {
                return false;
            }
        }
        return true;
    case KEST_EXPR_MATCH:
        for (uint32_t i = 0; i < expr->choose.subject_count; i++) {
            if (!expr_writes_no_arrays(compiler, expr->choose.subjects[i])) {
                return false;
            }
        }
        for (uint32_t a = 0; a < expr->choose.arm_count; a++) {
            if (!expr_writes_no_arrays(compiler, expr->choose.arms[a].value) ||
                !writes_no_arrays(compiler, &expr->choose.arms[a].body)) {
                return false;
            }
        }
        return true;
    case KEST_EXPR_IF: {
        const KestBranch *branch = expr->branch;
        return expr_writes_no_arrays(compiler, branch->condition) &&
               expr_writes_no_arrays(compiler, branch->then_value) &&
               writes_no_arrays(compiler, &branch->then_body) &&
               expr_writes_no_arrays(compiler, branch->otherwise) &&
               expr_writes_no_arrays(compiler, branch->else_value) &&
               writes_no_arrays(compiler, &branch->else_body);
    }
    default:
        return true;
    }
}

static bool writes_no_arrays(Compiler *compiler, const KestBlock *block) {
    for (uint32_t i = 0; i < block->count; i++) {
        const KestStmt *stmt = block->items[i];
        switch (stmt->kind) {
        case KEST_STMT_LET:
            if (!expr_writes_no_arrays(compiler, stmt->let.value)) {
                return false;
            }
            break;
        case KEST_STMT_ASSIGN:
            // Only a plain name can be written, because anything with an
            // index in it is a write into memory something else may be
            // walking.
            if (stmt->assign.target == NULL ||
                stmt->assign.target->kind != KEST_EXPR_NAME) {
                return false;
            }
            if (!expr_writes_no_arrays(compiler, stmt->assign.value)) {
                return false;
            }
            break;
        case KEST_STMT_EXPR:
            if (!expr_writes_no_arrays(compiler, stmt->value)) {
                return false;
            }
            break;
        case KEST_STMT_WHILE:
            if (!expr_writes_no_arrays(compiler, stmt->loop.condition) ||
                !writes_no_arrays(compiler, &stmt->loop.body)) {
                return false;
            }
            break;
        case KEST_STMT_FOR:
            if (!expr_writes_no_arrays(compiler, stmt->each.sequence) ||
                !expr_writes_no_arrays(compiler, stmt->each.until) ||
                !writes_no_arrays(compiler, &stmt->each.body)) {
                return false;
            }
            break;
        case KEST_STMT_RETURN:
            if (!expr_writes_no_arrays(compiler, stmt->result)) {
                return false;
            }
            break;
        case KEST_STMT_BLOCK:
            if (!writes_no_arrays(compiler, &stmt->block)) {
                return false;
            }
            break;
        default:
            break;
        }
    }
    return true;
}

// Whether an address can be worked out for this, asked before anything is
// emitted. `compile_address` emits as it goes, so a caller that has somewhere
// else to fall back to has to know beforehand rather than find out halfway.
static bool can_address(Compiler *compiler, const KestExpr *expr) {
    if (expr->kind == KEST_EXPR_INDEX && expr->index.object->type != NULL &&
        expr->index.object->type->tag == KEST_T_FIXED) {
        int64_t at = written_index(compiler, expr->index.index);
        return at >= 0 &&
               (uint64_t)at < expr->index.object->type->count &&
               can_address(compiler, expr->index.object);
    }
    if (expr->kind == KEST_EXPR_FIELD) {
        return can_address(compiler, expr->field.object) &&
               find_member(expr->field.object->type,
                           span_text(compiler, expr->field.name),
                           expr->field.name.length) != NULL;
    }
    // A name that holds where something is rather than the thing itself.
    if (expr->kind == KEST_EXPR_NAME) {
        Local *local = find_local(compiler, expr->span);
        return local != NULL && local->is_address;
    }
    if (expr->kind != KEST_EXPR_INDEX) {
        return false;
    }
    const KestType *sequence = expr->index.object->type;
    return sequence != NULL && sequence->tag == KEST_T_ARRAY;
}

static bool compile_address(Compiler *compiler, const KestExpr *expr,
                            uint16_t *offset) {
    if (expr->kind == KEST_EXPR_FIELD) {
        if (!compile_address(compiler, expr->field.object, offset)) {
            return false;
        }
        const KestMember *member =
            find_member(expr->field.object->type,
                        span_text(compiler, expr->field.name),
                        expr->field.name.length);
        if (member == NULL) {
            return false;
        }
        // Bytes, because an address points into memory laid out the way the
        // host lays it out, not into slots.
        *offset = (uint16_t)(*offset + member->byte_offset);
        return true;
    }

    if (expr->kind == KEST_EXPR_NAME) {
        Local *local = find_local(compiler, expr->span);
        if (local == NULL || !local->is_address) {
            return false;
        }
        stack_push(compiler, 1);
        emit_load(compiler, local->slot, 1, expr->span);
        *offset = 0;
        return true;
    }

    // One of that many, at a place written down, is a byte offset into the
    // run rather than a step worked out while running.
    if (expr->kind == KEST_EXPR_INDEX && expr->index.object->type != NULL &&
        expr->index.object->type->tag == KEST_T_FIXED) {
        const KestType *run = expr->index.object->type;
        int64_t at = written_index(compiler, expr->index.index);
        if (at < 0 || (uint64_t)at >= run->count ||
            !compile_address(compiler, expr->index.object, offset)) {
            return false;
        }
        *offset = (uint16_t)(*offset + at * run->element->byte_size);
        return true;
    }

    if (expr->kind != KEST_EXPR_INDEX) {
        return false;
    }

    const KestType *sequence = expr->index.object->type;
    if (sequence == NULL || sequence->tag != KEST_T_ARRAY) {
        return false;
    }

    compile_expr(compiler, expr->index.object);
    compile_expr(compiler, expr->index.index);
    stack_pop(compiler, 1);
    emit(compiler, KEST_OP_ELEM_ADDR, expr->span);
    emit_u16(compiler, layout_of(compiler, sequence->element), expr->span);
    *offset = 0;
    return true;
}

// A condition compiled for where it goes rather than for what it is. The
// answer to `a || b` in the place a jump reads is never built: each half
// jumps, so the `true` that was pushed and the jump over it are not there at
// all, and the comparison at the end of each half goes into its own jump.
//
// `when_true` says which way the jumps this leaves are taken. What falls
// through is the other answer.
static void branch_when(Compiler *compiler, const KestExpr *expr,
                        bool when_true, Exits *out) {
    if (expr != NULL && expr->kind == KEST_EXPR_UNARY &&
        expr->unary.op == KEST_TOK_BANG) {
        // Turning the question round is not an instruction here: it is asking
        // the other one.
        branch_when(compiler, expr->unary.operand, !when_true, out);
        return;
    }
    if (expr != NULL && expr->kind == KEST_EXPR_BINARY &&
        (expr->binary.op == KEST_TOK_PIPEPIPE ||
         expr->binary.op == KEST_TOK_AMPAMP)) {
        bool either = expr->binary.op == KEST_TOK_PIPEPIPE;
        if (either == when_true) {
            // `a || b` leaving when true, or `a && b` leaving when false:
            // either half decides it on its own, so both leave the same way.
            branch_when(compiler, expr->binary.left, when_true, out);
            branch_when(compiler, expr->binary.right, when_true, out);
            return;
        }
        // The other way round: the left side can only settle it by going the
        // other way, and that lands where the whole thing falls through.
        Exits settled = {0};
        branch_when(compiler, expr->binary.left, !when_true, &settled);
        branch_when(compiler, expr->binary.right, when_true, out);
        patch_exits(compiler, &settled, expr->span);
        return;
    }

    compile_expr(compiler, expr);
    stack_pop(compiler, 1);
    take_exit(compiler, out,
              emit_jump(compiler,
                        when_true ? KEST_OP_JUMP_TRUE : KEST_OP_JUMP_FALSE,
                        expr->span));
}

// The condition of an `if` or a `while`, which is the only place a boolean is
// wanted for where it goes rather than for what it is. An `if let` is not one
// of these: what it leaves on the stack is the value it bound.
static Exits compile_condition(Compiler *compiler, const KestExpr *expr,
                               bool binding) {
    Exits out = {NULL, 0, 0};
    if (!binding) {
        branch_when(compiler, expr, false, &out);
        return out;
    }
    // What an `if let` leaves on the stack is the value it bound, so the jump
    // that reads the tag is the one way out and the binding is under it.
    compile_expr(compiler, expr);
    stack_pop(compiler, 1);
    take_exit(compiler, &out,
              emit_jump(compiler, KEST_OP_JUMP_FALSE, expr->span));
    return out;
}

static void compile_binary(Compiler *compiler, const KestExpr *expr) {
    KestTokenKind op = expr->binary.op;
    KestSpan span = expr->span;

    // Short circuiting is control flow, not an operator: the right side is
    // only reached when the left did not already decide the answer.
    if (op == KEST_TOK_AMPAMP || op == KEST_TOK_PIPEPIPE) {
        compile_expr(compiler, expr->binary.left);
        if (op == KEST_TOK_PIPEPIPE) {
            emit(compiler, KEST_OP_NOT, span);
        }
        stack_pop(compiler, 1);
        uint32_t skip = emit_jump(compiler, KEST_OP_JUMP_FALSE, span);
        compile_expr(compiler, expr->binary.right);
        uint32_t done = emit_jump(compiler, KEST_OP_JUMP, span);
        patch_jump(compiler, skip, span);
        // The jump arrives here having discarded the left side, and this
        // pushes the answer in its place, so the depth is unchanged.
        emit(compiler, op == KEST_TOK_PIPEPIPE ? KEST_OP_TRUE : KEST_OP_FALSE,
             span);
        patch_jump(compiler, done, span);
        return;
    }

    // An optional against `none`, which asks the flag beside the value and
    // nothing else: the other side is not compiled at all, because what it
    // holds is not part of the question. What is left of the one that is
    // compiled is its last slot, which is the flag — rotated to the bottom of
    // the run and the rest dropped. See D727.
    if ((op == KEST_TOK_EQEQ || op == KEST_TOK_BANGEQ) &&
        (expr->binary.left->kind == KEST_EXPR_NONE) !=
            (expr->binary.right->kind == KEST_EXPR_NONE)) {
        const KestExpr *held = expr->binary.left->kind == KEST_EXPR_NONE
                                   ? expr->binary.right
                                   : expr->binary.left;
        if (held->type != NULL && held->type->tag == KEST_T_OPTIONAL) {
            uint16_t wide = value_slots(held->type);
            compile_expr(compiler, held);
            if (wide > 1) {
                emit(compiler, KEST_OP_ROTATE, span);
                emit_u16(compiler, wide, span);
                emit(compiler, KEST_OP_POPN, span);
                emit_u16(compiler, (uint16_t)(wide - 1), span);
                stack_pop(compiler, (uint16_t)(wide - 1));
            }
            // The flag says it holds something, which is what `!= none` asks.
            if (op == KEST_TOK_EQEQ) {
                emit(compiler, KEST_OP_NOT, span);
            }
            return;
        }
    }

    compile_expr(compiler, expr->binary.left);
    compile_expr(compiler, expr->binary.right);
    stack_pop(compiler, 1);

    // The operands decide the instruction, not the result: a comparison
    // returns `bool` whatever it compared.
    const KestType *operand = expr->binary.left->type;
    bool real = is_float(operand);
    bool narrow = is_narrow(operand);
    bool unsigned_int = is_unsigned(operand);
    bool text = operand != NULL && operand->tag == KEST_T_TEXT;

    // One list of operators, and what each does to the width beside what it
    // emits. It was two lists — which instruction, and then which of them
    // leave the width — and the second had a `default` under it, so an
    // operator added to the first would leave the width without anybody
    // deciding it should. `&`, `|`, `^` and `>>` need no narrowing: every bit
    // they produce was already in range (D018).
    switch (op) {
    case KEST_TOK_PLUS:
        emit(compiler, real ? (narrow ? KEST_OP_ADD_F32 : KEST_OP_ADD_F)
                    : KEST_OP_ADD_I,
             span);
        emit_narrow(compiler, operand, span);
        break;
    case KEST_TOK_MINUS:
        emit(compiler, real ? (narrow ? KEST_OP_SUB_F32 : KEST_OP_SUB_F)
                    : KEST_OP_SUB_I,
             span);
        emit_narrow(compiler, operand, span);
        break;
    case KEST_TOK_STAR:
        emit(compiler, real ? (narrow ? KEST_OP_MUL_F32 : KEST_OP_MUL_F)
                    : KEST_OP_MUL_I,
             span);
        emit_narrow(compiler, operand, span);
        break;
    case KEST_TOK_SLASH:
        emit(compiler,
             real ? (narrow ? KEST_OP_DIV_F32 : KEST_OP_DIV_F)
                  : (unsigned_int ? KEST_OP_DIV_U : KEST_OP_DIV_I),
             span);
        // Once, and only for the pair at the end of the range: the least
        // number over minus one is one past the top of the width.
        emit_narrow(compiler, operand, span);
        break;
    case KEST_TOK_PERCENT:
        emit(compiler, unsigned_int ? KEST_OP_MOD_U : KEST_OP_MOD_I, span);
        break;
    case KEST_TOK_AMP:
        emit(compiler, KEST_OP_AND_I, span);
        break;
    case KEST_TOK_PIPE:
        emit(compiler, KEST_OP_OR_I, span);
        break;
    case KEST_TOK_CARET:
        emit(compiler, KEST_OP_XOR_I, span);
        break;
    case KEST_TOK_LTLT:
        emit(compiler, KEST_OP_SHL, span);
        emit_narrow(compiler, operand, span);
        break;
    case KEST_TOK_GTGT:
        // What shifts in on the right is the sign when there is one, and
        // nought when there is not, which is what the two types mean.
        emit(compiler, unsigned_int ? KEST_OP_SHR_U : KEST_OP_SHR_I, span);
        break;
    // Both are a run of slots and the answer is one, so the depth after is
    // one below where a scalar compare would leave it.
    case KEST_TOK_EQEQ:
    case KEST_TOK_BANGEQ:
        if (operand != NULL && operand->tag == KEST_T_ENUM) {
            stack_pop(compiler, (uint16_t)((operand->slots - 1) * 2));
            emit(compiler,
                 op == KEST_TOK_EQEQ ? KEST_OP_EQ_ENUM : KEST_OP_NE_ENUM,
                 span);
            emit_u16(compiler, layout_of(compiler, operand), span);
            break;
        }
        // fall through
    case KEST_TOK_LT:
    case KEST_TOK_LTEQ:
    case KEST_TOK_GT:
    case KEST_TOK_GTEQ: {
        const uint8_t *how = compares(op, text, real, unsigned_int);
        if (how == NULL) {
            fault(compiler, span, "this is an operator with no instruction");
            return;
        }
        emit(compiler, *how, span);
        break;
    }
    default:
        fault(compiler, span, "this is an operator with no instruction");
        return;
    }

}

// A builtin is what a name means when nothing was declared under it, which
// the checker decided and left on the callee.
static bool builtin_named(Compiler *compiler, const char *name, size_t length,
                          const char *word) {
    (void)compiler;
    return strlen(word) == length && memcmp(name, word, length) == 0;
}

// The arguments are already on the stack in the order they were written, so
// each of these is one instruction over them.
static bool compile_builtin(Compiler *compiler, const KestExpr *expr,
                            const char *name, size_t length) {
    if (builtin_named(compiler, name, length, "len")) {
        const KestType *subject =
            expr->call.arg_count > 0 ? expr->call.args[0]->type : NULL;
        // How many of them is written in the type, so the answer is a
        // constant and what was counted is dropped.
        if (subject != NULL && subject->tag == KEST_T_FIXED) {
            uint16_t held = value_slots(subject);
            stack_pop(compiler, held);
            emit(compiler, KEST_OP_POPN, expr->span);
            emit_u16(compiler, held, expr->span);
            KestValue how_many = {0};
            how_many.integer = subject->count;
            stack_push(compiler, 1);
            emit_constant(compiler, how_many, KEST_CONST_INT, expr->span);
            return true;
        }
        KestOp op = KEST_OP_LEN;
        if (subject != NULL && subject->tag == KEST_T_STORE) {
            op = KEST_OP_COUNT;
        } else if (subject != NULL && subject->tag == KEST_T_TEXT) {
            op = KEST_OP_TEXT_LEN;
        }
        emit(compiler, op, expr->span);
        return true;
    }

    if (builtin_named(compiler, name, length, "slice")) {
        stack_pop(compiler, 3);
        stack_push(compiler, 1);
        emit(compiler, KEST_OP_TEXT_SLICE, expr->span);
        return true;
    }

    if (builtin_named(compiler, name, length, "matches")) {
        stack_pop(compiler, 3);
        stack_push(compiler, 1);
        emit(compiler, KEST_OP_TEXT_MATCHES, expr->span);
        return true;
    }

    if (builtin_named(compiler, name, length, "rest")) {
        stack_pop(compiler, 2);
        stack_push(compiler, 1);
        emit(compiler, KEST_OP_TEXT_REST, expr->span);
        return true;
    }

    if (builtin_named(compiler, name, length, "find")) {
        // Where to look from, which is the beginning when it was not said.
        // The instruction takes three either way, so there is one of it.
        if (expr->call.arg_count < 3) {
            KestValue zero = {0};
            emit_constant(compiler, zero, KEST_CONST_INT, expr->span);
        }
        stack_pop(compiler, 3);
        stack_push(compiler, 2);
        emit(compiler, KEST_OP_TEXT_FIND, expr->span);
        return true;
    }

    if (builtin_named(compiler, name, length, "array")) {
        const KestType *element =
            expr->type == NULL ? NULL : expr->type->element;
        // An empty one has nothing to fill it with, and the instruction reads
        // a fill whether it uses it or not, so it gets a nought of the right
        // width and never looks at it.
        if (expr->call.arg_count == 0) {
            KestValue zero = {0};
            emit_constant(compiler, zero, KEST_CONST_INT, expr->span);
            for (uint16_t i = 0; i < value_slots(element); i++) {
                emit_constant(compiler, zero, KEST_CONST_INT, expr->span);
            }
            stack_push(compiler, (uint16_t)(1 + value_slots(element)));
        }
        stack_pop(compiler, (uint16_t)(1 + value_slots(element)));
        stack_push(compiler, 1);
        emit(compiler, KEST_OP_MAKE_ARRAY, expr->span);
        emit_u16(compiler, layout_of(compiler, element), expr->span);
        return true;
    }

    const KestType *shrinking =
        expr->call.arg_count > 0 ? expr->call.args[0]->type : NULL;
    // `remove` answers for a store too, further down, so this asks what it was
    // handed rather than only what it was called.
    if ((builtin_named(compiler, name, length, "pop") ||
         builtin_named(compiler, name, length, "remove") ||
         builtin_named(compiler, name, length, "clear")) &&
        shrinking != NULL && shrinking->tag == KEST_T_ARRAY) {
        const KestType *array = shrinking;
        const KestType *element = array->element;
        if (builtin_named(compiler, name, length, "clear")) {
            stack_pop(compiler, 1);
            emit(compiler, KEST_OP_CLEAR, expr->span);
            return true;
        }
        bool taking = builtin_named(compiler, name, length, "remove");
        stack_pop(compiler, taking ? 2 : 1);
        stack_push(compiler,
                   (uint16_t)(value_slots(element) + (taking ? 0 : 1)));
        emit(compiler, taking ? KEST_OP_TAKE : KEST_OP_POP_LAST, expr->span);
        emit_u16(compiler, layout_of(compiler, element), expr->span);
        return true;
    }

    if (builtin_named(compiler, name, length, "hash")) {
        const KestType *of =
            expr->call.arg_count > 0 ? expr->call.args[0]->type : NULL;
        if (of != NULL && of->tag == KEST_T_ENUM) {
            stack_pop(compiler, (uint16_t)(value_slots(of) - 1));
            emit(compiler, KEST_OP_HASH_ENUM, expr->span);
            emit_u16(compiler, layout_of(compiler, of), expr->span);
            return true;
        }
        emit(compiler,
             of != NULL && of->tag == KEST_T_TEXT
                 ? KEST_OP_HASH_T
                 : (of != NULL && of->tag == KEST_T_FLOAT ? KEST_OP_HASH_F
                                                          : KEST_OP_HASH_I),
             expr->span);
        return true;
    }

    if (builtin_named(compiler, name, length, "push")) {
        const KestType *array =
            expr->call.arg_count > 0 ? expr->call.args[0]->type : NULL;
        const KestType *element = array == NULL ? NULL : array->element;
        stack_pop(compiler, (uint16_t)(1 + value_slots(element)));
        emit(compiler, KEST_OP_PUSH, expr->span);
        emit_u16(compiler, layout_of(compiler, element), expr->span);
        return true;
    }

    if (builtin_named(compiler, name, length, "store")) {
        uint16_t stride = expr->type == NULL || expr->type->element == NULL
                              ? 1
                              : value_slots(expr->type->element);
        // The instruction reads how much room to make either way, so one that
        // was not asked for gets a nought, the way an empty `array()` gets a
        // fill it never looks at.
        if (expr->call.arg_count == 0) {
            KestValue zero = {0};
            emit_constant(compiler, zero, KEST_CONST_INT, expr->span);
        }
        stack_pop(compiler, 1);
        stack_push(compiler, 1);
        emit(compiler, KEST_OP_NEW_STORE, expr->span);
        emit_u16(compiler, stride, expr->span);
        return true;
    }

    bool adding = builtin_named(compiler, name, length, "add");
    bool getting = builtin_named(compiler, name, length, "get");
    bool setting = builtin_named(compiler, name, length, "set");
    bool removing = builtin_named(compiler, name, length, "remove");
    if (!adding && !getting && !setting && !removing) {
        return false;
    }

    const KestType *store =
        expr->call.arg_count > 0 ? expr->call.args[0]->type : NULL;
    uint16_t stride = store == NULL || store->element == NULL
                          ? 1
                          : value_slots(store->element);

    if (adding) {
        stack_pop(compiler, (uint16_t)(1 + stride));
        stack_push(compiler, 1);
        emit(compiler, KEST_OP_ADD, expr->span);
        emit_u16(compiler, stride, expr->span);
    } else if (getting) {
        stack_pop(compiler, 2);
        stack_push(compiler, (uint16_t)(stride + 1));
        emit(compiler, KEST_OP_GET, expr->span);
        emit_u16(compiler, stride, expr->span);
    } else if (setting) {
        stack_pop(compiler, (uint16_t)(2 + stride));
        stack_push(compiler, 1);
        emit(compiler, KEST_OP_SET, expr->span);
        emit_u16(compiler, stride, expr->span);
    } else {
        stack_pop(compiler, 2);
        stack_push(compiler, 1);
        emit(compiler, KEST_OP_REMOVE, expr->span);
    }
    return true;
}

// The argument is already on the stack, so a conversion is what has to happen
// to it and nothing else.
static void compile_conversion(Compiler *compiler, const KestExpr *expr,
                               const KestType *to) {
    if (expr->call.arg_count != 1) {
        return;
    }
    const KestType *from = expr->call.args[0]->type;
    if (from == NULL) {
        return;
    }
    bool from_real = from->tag == KEST_T_FLOAT;

    if (to->tag == KEST_T_INT) {
        if (from_real) {
            emit(compiler, KEST_OP_F2I, expr->span);
            emit_u16(compiler, kest_scalar_of(to), expr->span);
        } else {
            // A `bool` is already nought or one, and an integer only has to
            // be cut to the width it is going into.
            emit_narrow(compiler, to, expr->span);
        }
        return;
    }

    if (!from_real) {
        emit(compiler,
             from->tag == KEST_T_INT && !from->is_signed ? KEST_OP_U2F
                                                         : KEST_OP_I2F,
             expr->span);
    }
    // A slot holds a double either way, so widening is nothing and narrowing
    // is a rounding.
    if (to->width == 32) {
        emit(compiler, KEST_OP_TO_F32, expr->span);
    }
}

static const KestVariantType *case_named(const KestType *choice,
                                         const char *name, size_t length) {
    for (uint32_t i = 0; i < choice->case_count; i++) {
        if (strlen(choice->cases[i].name) == length &&
            memcmp(choice->cases[i].name, name, length) == 0) {
            return &choice->cases[i];
        }
    }
    return NULL;
}

// A case is its tag and its payload, padded out to whatever the widest case
// needs, because every case of one enum is the same size.
static void compile_case_tail(Compiler *compiler, const KestExpr *expr,
                              const KestType *choice, KestSpan name) {
    const KestVariantType *variant =
        case_named(choice, span_text(compiler, name), name.length);
    if (variant == NULL) {
        return;
    }

    uint16_t carried = 0;
    for (uint32_t i = 0; i < variant->payload_count; i++) {
        carried += value_slots(variant->payload[i]);
    }
    uint16_t slack = (uint16_t)(choice->slots - 1 - carried);
    KestValue zero = {0};
    for (uint16_t i = 0; i < slack; i++) {
        emit_constant(compiler, zero, KEST_CONST_INT, expr->span);
    }

    KestValue tag = {0};
    tag.integer = (int64_t)(variant - choice->cases);
    emit_constant(compiler, tag, KEST_CONST_INT, expr->span);

    // The tag was pushed last and belongs first, so the whole value is turned
    // over: what is on the stack is payload then tag, and what a slot run is
    // is tag then payload.
    stack_pop(compiler, (uint16_t)(carried + slack + 1));
    stack_push(compiler, choice->slots);
    emit(compiler, KEST_OP_ROTATE, expr->span);
    emit_u16(compiler, choice->slots, expr->span);
}

// Through a value: the arguments are on the stack, then which function it is,
// which the instruction takes off the top. What it promises is in its type, so
// a cost contract holds without knowing which function it will be.
static void compile_value_call(Compiler *compiler, const KestExpr *expr) {
    uint16_t through = 0;
    for (uint32_t i = 0; i < expr->call.arg_count; i++) {
        through += value_slots(expr->call.args[i]->type);
    }
    const KestType *shape = expr->call.callee->type;
    compile_expr(compiler, expr->call.callee);
    stack_pop(compiler, (uint16_t)(through + 1));
    // What it gives rather than what the expression is, for the reason above.
    stack_push(compiler, shape != NULL && shape->tag == KEST_T_FN
                             ? value_slots(shape->result)
                             : value_slots(expr->type));
    emit(compiler, KEST_OP_CALL_VALUE, expr->span);
    emit_u16(compiler, through, expr->span);
}

static void compile_call(Compiler *compiler, const KestExpr *expr) {
    const KestExpr *callee = expr->call.callee;

    // A function is a value, so it is reached the way a value is: out of an
    // array, out of a store, out of whatever holds it. Only a name and a
    // dotted name are looked up as names, and what is left is called through
    // what it is.
    if (callee->kind != KEST_EXPR_NAME && callee->kind != KEST_EXPR_FIELD &&
        callee->type != NULL && callee->type->tag == KEST_T_FN &&
        !callee->type->is_foreign) {
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            compile_expr(compiler, expr->call.args[i]);
        }
        compile_value_call(compiler, expr);
        return;
    }

    // A dotted callee is a function in another module, or an extern named for
    // its host type. Both are one name with a dot in it.
    if (callee->kind != KEST_EXPR_NAME && callee->kind != KEST_EXPR_FIELD) {
        fault(compiler, callee->span, "this calls something that is not a "
                                     "function");
        return;
    }

    // `len` of a `[T; N]` is a number in the type, and what it was given is
    // dropped. A name has nothing to do but be loaded, so a run of two
    // hundred and fifty-six slots was copied onto the stack to be thrown
    // away. Anything else is still worked out: a call in there is the point
    // of the line as often as not.
    const KestExpr *only = expr->call.arg_count == 1 ? expr->call.args[0] : NULL;
    bool is_a_function =
        callee->type != NULL && callee->type->tag == KEST_T_FN &&
        callee->type->symbol != NULL &&
        kest_module_find(compiler->module, callee->type->symbol) >= 0;
    if (!is_a_function && only != NULL && only->kind == KEST_EXPR_NAME &&
        only->type != NULL && only->type->tag == KEST_T_FIXED &&
        callee->kind == KEST_EXPR_NAME &&
        builtin_named(compiler, span_text(compiler, callee->span),
                      callee->span.length, "len")) {
        KestValue how_many = {0};
        how_many.integer = only->type->count;
        stack_push(compiler, 1);
        emit_constant(compiler, how_many, KEST_CONST_INT, expr->span);
        return;
    }

    for (uint32_t i = 0; i < expr->call.arg_count; i++) {
        compile_expr(compiler, expr->call.args[i]);
    }

    // Building a struct emits nothing. Its fields were pushed in declaration
    // order, which is the layout, so the value is already on the stack.
    if (callee->type != NULL && callee->type->tag == KEST_T_STRUCT) {
        return;
    }
    if (callee->type != NULL && callee->type->tag == KEST_T_ENUM) {
        // The arguments are already on the stack where the payload goes; the
        // rest of the value is the tag under them and nothing above.
        compile_case_tail(compiler, expr, callee->type,
                          callee->kind == KEST_EXPR_FIELD ? callee->field.name
                                                          : callee->span);
        return;
    }
    if (callee->type != NULL && (callee->type->tag == KEST_T_INT ||
                                 callee->type->tag == KEST_T_FLOAT)) {
        compile_conversion(compiler, expr, callee->type);
        return;
    }
    if (callee->type != NULL && callee->type->tag == KEST_T_FLAGS) {
        if (expr->call.arg_count == 0) {
            KestValue empty = {0};
            emit_constant(compiler, empty, KEST_CONST_INT, expr->span);
        }
        // A set made from a number of the same width is the same bits, so
        // there is nothing to emit over what is already on the stack.
        return;
    }
    if (callee->type != NULL && callee->type->tag == KEST_T_TEXT &&
        expr->call.arg_count == 1) {
        emit(compiler, KEST_OP_TEXT_FROM, expr->span);
        return;
    }

    // Through a value: the arguments are on the stack, then which function it
    // is, which the instruction takes off the top.
    // A local holds a function rather than being one, and a function type
    // with no symbol is a value rather than a declaration. `io.print` is a
    // declaration with a dot in its name and goes the other way.
    bool through_value =
        callee->type != NULL && callee->type->tag == KEST_T_FN &&
        !callee->type->is_foreign &&
        (callee->type->symbol == NULL ||
         (callee->kind == KEST_EXPR_NAME &&
          find_local(compiler, callee->span) != NULL));
    if (through_value) {
        compile_value_call(compiler, expr);
        return;
    }

    const char *name = span_text(compiler, callee->span);
    uint16_t argument_slots = 0;
    for (uint32_t i = 0; i < expr->call.arg_count; i++) {
        argument_slots += value_slots(expr->call.args[i]->type);
    }
    // What the function gives, which is not always what the expression is: a
    // value standing where an optional is wanted is widened by the checker and
    // the tag is emitted after the call, so asking the expression here would
    // count the tag twice — and a host call carries this number, so it would
    // write over the slot beside its answer.
    uint16_t result_slots =
        callee->type != NULL && callee->type->tag == KEST_T_FN
            ? value_slots(callee->type->result)
            : value_slots(expr->type);

    // The checker already settled which function this is, and its symbol is
    // what it was compiled under, so nothing is chosen twice. A file that
    // declares its own `find` gets that one here for the same reason it got
    // it there.
    int32_t index = -1;
    if (callee->type != NULL && callee->type->tag == KEST_T_FN &&
        callee->type->symbol != NULL) {
        index = kest_module_find(compiler->module, callee->type->symbol);
    }
    if (index < 0 &&
        compile_builtin(compiler, expr, name, callee->span.length)) {
        return;
    }
    if (index >= 0) {
        stack_pop(compiler, argument_slots);
        stack_push(compiler, result_slots);
        emit(compiler, KEST_OP_CALL, expr->span);
        emit_u16(compiler, (uint16_t)index, expr->span);
        emit_u16(compiler, argument_slots, expr->span);
        return;
    }

    // Not defined here, so it is declared: the host provides it, and which
    // one it is is settled by name before the program runs.
    const KestType *foreign = callee->type;
    if (foreign == NULL || foreign->tag != KEST_T_FN || !foreign->is_foreign) {
        fault(compiler, callee->span,
              "this calls a function with no body and no host to provide it");
        return;
    }

    const KestSymbol *declared =
        kest_lookup_global(compiler->program, name, callee->span.length);
    int32_t slot = kest_module_extern(
        compiler->module, foreign->foreign_name,
        declared == NULL ? callee->span : declared->span,
        declared == NULL ? compiler->program->source : declared->source,
        foreign->no_alloc);
    if (slot < 0) {
        compiler->out_of_memory = true;
        return;
    }
    if (slot >= MAX_EXTERNS) {
        // The one below is written into the call in two bytes, so this is the
        // last one there is room to name.
        refuse(compiler, callee->span, "K0502",
               "a program asks the host for at most %d names", MAX_EXTERNS);
        return;
    }
    // What the program expects to cross, written down where a host can read
    // it: the same layouts a function of the program's own carries, because a
    // crossing is the same shape whichever way it goes.
    if (foreign->param_count > 0 || foreign->result != NULL) {
        uint16_t *widths = NULL;
        if (foreign->param_count > 0) {
            widths = KEST_ARENA_ARRAY(compiler->module->arena, uint16_t,
                                      foreign->param_count);
            if (widths == NULL) {
                compiler->out_of_memory = true;
                return;
            }
            for (uint32_t p = 0; p < foreign->param_count; p++) {
                widths[p] = layout_of(compiler, foreign->params[p]);
            }
        }
        bool gives_value =
            foreign->result != NULL && foreign->result->tag != KEST_T_VOID;
        kest_module_extern_shape(
            compiler->module, (uint32_t)slot, widths,
            (uint16_t)foreign->param_count,
            gives_value ? layout_of(compiler, foreign->result) : 0,
            gives_value);
    }
    stack_pop(compiler, argument_slots);
    stack_push(compiler, result_slots);
    emit(compiler, KEST_OP_CALL_HOST, expr->span);
    emit_u16(compiler, (uint16_t)slot, expr->span);
    emit_u16(compiler, argument_slots, expr->span);
    emit_u16(compiler, result_slots, expr->span);
}

static void compile_expr_kind(Compiler *compiler, const KestExpr *expr) {
    switch (expr->kind) {
    case KEST_EXPR_INT: {
        KestValue value = {0};
        // The lexer's reader, which the checker also uses, because a `u64`
        // literal does not fit in the signed accumulator this used to have.
        bool overflow = false;
        value.integer = (int64_t)kest_token_integer(
            span_text(compiler, expr->span), expr->span.length, &overflow);
        emit_constant(compiler, value, KEST_CONST_INT, expr->span);
        break;
    }
    case KEST_EXPR_FLOAT: {
        KestValue value = {0};
        value.real = parse_real(compiler, expr->span);
        // An `f32` literal is the nearest `f32`, not the nearest double that
        // happens to be spelled the same way.
        if (is_narrow(expr->type)) {
            value.real = (float)value.real;
        }
        emit_constant(compiler, value, KEST_CONST_FLOAT, expr->span);
        break;
    }
    case KEST_EXPR_STRING: {
        KestValue value = {0};
        KestSpan content = {expr->span.offset + 1, expr->span.length - 2};
        value.text = literal_text(compiler, content);
        emit_constant(compiler, value, KEST_CONST_TEXT, expr->span);
        break;
    }
    case KEST_EXPR_BYTE: {
        // The same escapes a string has, read the same way, so a byte written
        // in one and a byte written on its own are one spelling.
        KestSpan content = {expr->span.offset + 1, expr->span.length - 2};
        const char *held = literal_text(compiler, content);
        KestValue value = {0};
        value.integer = (unsigned char)held[0];
        stack_push(compiler, 1);
        emit_constant(compiler, value, KEST_CONST_INT, expr->span);
        break;
    }
    case KEST_EXPR_BOOL:
        stack_push(compiler, 1);
        emit(compiler, expr->boolean ? KEST_OP_TRUE : KEST_OP_FALSE,
             expr->span);
        break;

    case KEST_EXPR_NONE: {
        // An optional is what it holds with a tag after it, so the missing
        // case is that many slots of nothing and a tag that says so.
        uint16_t size = value_slots(expr->type);
        KestValue zero = {0};
        for (uint16_t i = 1; i < size; i++) {
            emit_constant(compiler, zero, KEST_CONST_INT, expr->span);
        }
        stack_push(compiler, 1);
        emit(compiler, KEST_OP_FALSE, expr->span);
        break;
    }
    case KEST_EXPR_NAME: {
        Local *local = find_local(compiler, expr->span);
        if (local == NULL) {
            compile_constant(compiler, expr);
            break;
        }
        stack_push(compiler, local->size);
        emit_load(compiler, local->slot, local->size, expr->span);
        break;
    }
    case KEST_EXPR_UNARY:
        compile_expr(compiler, expr->unary.operand);
        if (expr->unary.op == KEST_TOK_BANG) {
            emit(compiler, KEST_OP_NOT, expr->span);
        } else if (expr->unary.op == KEST_TOK_TILDE) {
            emit(compiler, KEST_OP_NOT_I, expr->span);
            // `~0` is every bit of the width it is declared at, so a `u8` one
            // is 255 rather than the slot's -1.
            emit_narrow(compiler, expr->type, expr->span);
        } else {
            emit(compiler,
                 is_float(expr->type)
                     ? (is_narrow(expr->type) ? KEST_OP_NEG_F32 : KEST_OP_NEG_F)
                     : KEST_OP_NEG_I,
                 expr->span);
            emit_narrow(compiler, expr->type, expr->span);
        }
        break;
    case KEST_EXPR_BINARY:
        compile_binary(compiler, expr);
        break;
    case KEST_EXPR_CALL:
        compile_call(compiler, expr);
        break;
    case KEST_EXPR_FIELD: {
        if (compile_folded(compiler, expr)) {
            break;
        }
        // `sort.ascending` is one name with a dot in it, not a field of a
        // `sort`, and where a value is wanted it is which function it is.
        if (compile_function_value(compiler, expr)) {
            break;
        }
        // `box.CELLS` is one name with a dot in it, the same way
        // `sort.ascending` is: a constant another module declared, which
        // crosses out of the file it is in. See D665.
        const KestSymbol *elsewhere = kest_lookup_global(
            compiler->program, span_text(compiler, expr->span),
            expr->span.length);
        if (elsewhere != NULL && elsewhere->is_const) {
            compile_constant(compiler, expr);
            break;
        }
        // A named bit is a constant: which bit it is, is where it was
        // written, so nothing is stored and nothing can drift.
        if (expr->field.object->type != NULL &&
            expr->field.object->type->tag == KEST_T_FLAGS) {
            const KestType *set = expr->field.object->type;
            const KestVariantType *bit =
                case_named(set, span_text(compiler, expr->field.name),
                           expr->field.name.length);
            KestValue value = {0};
            if (bit != NULL) {
                value.integer = (int64_t)1 << (bit - set->cases);
            }
            emit_constant(compiler, value, KEST_CONST_INT, expr->span);
            break;
        }
        if (expr->field.object->type != NULL &&
            expr->field.object->type->tag == KEST_T_ENUM) {
            compile_case_tail(compiler, expr, expr->field.object->type,
                              expr->field.name);
            break;
        }
        uint16_t slot = 0;
        uint16_t size = 0;
        if (resolve_place(compiler, expr, &slot, &size)) {
            stack_push(compiler, size);
            emit_load(compiler, slot, size, expr->span);
            break;
        }
        // A field of something that has an address is read from that address.
        // Otherwise the whole value would be unpacked out of the host's bytes
        // to keep one piece of it, which is what a frame reads most.
        uint16_t offset = 0;
        if (can_address(compiler, expr) &&
            compile_address(compiler, expr, &offset)) {
            stack_pop(compiler, 1);
            stack_push(compiler, value_slots(expr->type));
            emit(compiler, KEST_OP_LOAD_AT, expr->span);
            emit_u16(compiler, offset, expr->span);
            emit_u16(compiler, layout_of(compiler, expr->type), expr->span);
            break;
        }
        // The struct is not in a slot, so it has to be built on the stack and
        // the member kept out of it.
        const KestMember *member =
            find_member(expr->field.object->type,
                        span_text(compiler, expr->field.name),
                        expr->field.name.length);
        if (member == NULL) {
            fault(compiler, expr->span,
                  "this reads a field the type does not have");
            break;
        }
        uint16_t total = value_slots(expr->field.object->type);
        uint16_t kept = value_slots(member->type);
        compile_expr(compiler, expr->field.object);
        stack_pop(compiler, (uint16_t)(total - kept));
        emit(compiler, KEST_OP_FIELD, expr->span);
        emit_u16(compiler, member->offset, expr->span);
        emit_u16(compiler, kept, expr->span);
        emit_u16(compiler, total, expr->span);
        break;
    }
    case KEST_EXPR_INDEX: {
        if (compile_folded(compiler, expr)) {
            break;
        }
        const KestType *object = expr->index.object->type;
        // That many of something is a value, so one of them is at a slot the
        // index works out rather than behind a handle.
        if (object != NULL && object->tag == KEST_T_FIXED) {
            uint16_t stride = value_slots(object->element);
            uint16_t slot = 0;
            uint16_t size = 0;
            // An index written down is a slot, the same way a field is, or a
            // byte offset where the run is memory the host laid out.
            if (resolve_place(compiler, expr, &slot, &size)) {
                stack_push(compiler, size);
                emit_load(compiler, slot, size, expr->span);
                break;
            }
            uint16_t written = 0;
            if (can_address(compiler, expr) &&
                compile_address(compiler, expr, &written)) {
                stack_pop(compiler, 1);
                stack_push(compiler, stride);
                emit(compiler, KEST_OP_LOAD_AT, expr->span);
                emit_u16(compiler, written, expr->span);
                emit_u16(compiler, layout_of(compiler, object->element),
                         expr->span);
                break;
            }
            if (resolve_place(compiler, expr->index.object, &slot, &size)) {
                compile_expr(compiler, expr->index.index);
                stack_pop(compiler, 1);
                stack_push(compiler, stride);
                emit(compiler, KEST_OP_LOAD_SLOTS, expr->span);
                emit_u16(compiler, slot, expr->span);
                emit_u16(compiler, stride, expr->span);
                emit_u16(compiler, (uint16_t)object->count, expr->span);
                break;
            }
            uint16_t offset = 0;
            if (can_address(compiler, expr->index.object) &&
                compile_address(compiler, expr->index.object, &offset)) {
                // The address of the run, then one step into it.
                if (offset > 0) {
                    KestValue nothing = {0};
                    nothing.integer = 0;
                    emit_constant(compiler, nothing, KEST_CONST_INT,
                                  expr->span);
                    stack_push(compiler, 1);
                    stack_pop(compiler, 1);
                    emit(compiler, KEST_OP_OFFSET_ADDR, expr->span);
                    emit_u16(compiler, offset, expr->span);
                    emit_u16(compiler, 1, expr->span);
                }
                compile_expr(compiler, expr->index.index);
                stack_pop(compiler, 1);
                emit(compiler, KEST_OP_OFFSET_ADDR, expr->span);
                emit_u16(compiler, object->element->byte_size, expr->span);
                emit_u16(compiler, (uint16_t)object->count, expr->span);
                stack_pop(compiler, 1);
                stack_push(compiler, stride);
                emit(compiler, KEST_OP_LOAD_AT, expr->span);
                emit_u16(compiler, 0, expr->span);
                emit_u16(compiler, layout_of(compiler, object->element),
                         expr->span);
                break;
            }
            // The run is a constant, so it is in the chunk already: one of
            // it is read there rather than copied into slots to be read back.
            uint16_t wide = object->slots;
            KestValue *held =
                KEST_ARENA_ARRAY(compiler->program->arena, KestValue,
                                 wide == 0 ? 1 : wide);
            const char *why = NULL;
            if (held != NULL && wide > 0 &&
                kest_fold_const(compiler->program, expr->index.object, held,
                                wide, &why, NULL) == wide) {
                uint32_t first = 0;
                if (!constant_run(compiler, object, held, wide, &first)) {
                    break;
                }
                if (compiler->chunk != NULL) {
                    compiler->chunk->folded++;
                    compiler->chunk->folded_slots += wide;
                }
                compile_expr(compiler, expr->index.index);
                stack_pop(compiler, 1);
                stack_push(compiler, stride);
                emit(compiler, KEST_OP_CONST_AT, expr->span);
                emit_u16(compiler, (uint16_t)first, expr->span);
                emit_u16(compiler, stride, expr->span);
                emit_u16(compiler, (uint16_t)object->count, expr->span);
                break;
            }

            // Not a place and not an address, which is what a value where
            // it stands is: what a call gave back. It goes into slots of its
            // own and is indexed there, the same way a walk of one copies it
            // before walking it.
            if (object->slots > 0) {
                uint16_t held = reserve_slot(compiler, object->slots);
                compile_expr(compiler, expr->index.object);
                stack_pop(compiler, object->slots);
                emit_store(compiler, held, object->slots, expr->span);
                compile_expr(compiler, expr->index.index);
                stack_pop(compiler, 1);
                stack_push(compiler, stride);
                emit(compiler, KEST_OP_LOAD_SLOTS, expr->span);
                emit_u16(compiler, held, expr->span);
                emit_u16(compiler, stride, expr->span);
                emit_u16(compiler, (uint16_t)object->count, expr->span);
                break;
            }
            fault(compiler, expr->span, "this indexes a run of nothing");
            break;
        }
        compile_expr(compiler, expr->index.object);
        compile_expr(compiler, expr->index.index);
        stack_pop(compiler, 2);
        stack_push(compiler, value_slots(expr->type));
        if (object != NULL && object->tag == KEST_T_TEXT) {
            emit(compiler, KEST_OP_TEXT_AT, expr->span);
            break;
        }
        emit(compiler, KEST_OP_INDEX, expr->span);
        emit_u16(compiler, layout_of(compiler, expr->type), expr->span);
        break;
    }

    case KEST_EXPR_TEXT: {
        for (uint32_t i = 0; i < expr->text.count; i++) {
            const KestTextPart *part = &expr->text.parts[i];
            if (part->value == NULL) {
                KestValue value = {0};
                value.text = literal_text(compiler, part->text);
                emit_constant(compiler, value, KEST_CONST_TEXT, expr->span);
                continue;
            }
            compile_expr(compiler, part->value);
            const KestType *type = part->value->type;
            if (type == NULL || type->tag == KEST_T_TEXT) {
                continue;
            }
            // A set of bits is written the way it is built, so the names it
            // holds have to come with it. The layout already carries the
            // type, so nothing new is stored for it.
            if (type->tag == KEST_T_FLAGS || type->tag == KEST_T_ENUM ||
                type->tag == KEST_T_OPTIONAL) {
                // A run of slots whose text is one, so what the walk of the
                // parts counts has to come back to one. An optional is the
                // same shape: what it holds, and a tag after it.
                stack_pop(compiler, (uint16_t)(value_slots(type) - 1));
                emit(compiler,
                     type->tag == KEST_T_FLAGS ? KEST_OP_TEXT_FLAGS
                                               : KEST_OP_TEXT_ENUM,
                     expr->span);
                emit_u16(compiler, layout_of(compiler, type), expr->span);
                continue;
            }
            emit(compiler,
                 type->tag == KEST_T_FLOAT
                     ? (is_narrow(type) ? KEST_OP_TEXT_F32 : KEST_OP_TEXT_F)
                     : (type->tag == KEST_T_BOOL
                            ? KEST_OP_TEXT_B
                            : (is_unsigned(type) ? KEST_OP_TEXT_U
                                                 : KEST_OP_TEXT_I)),
                 expr->span);
        }
        stack_pop(compiler, (uint16_t)expr->text.count);
        stack_push(compiler, 1);
        emit(compiler, KEST_OP_CONCAT, expr->span);
        emit_u16(compiler, (uint16_t)expr->text.count, expr->span);
        break;
    }

    case KEST_EXPR_IF: {
        const KestBranch *branch = expr->branch;
        uint16_t gives = branch->gives ? value_slots(expr->type) : 0;

        Exits otherwise =
            compile_condition(compiler, branch->condition,
                              branch->binding.length > 0);

        // `if let` leaves what the optional held below the tag the jump
        // consumed. The taken arm binds it; the other arm drops it.
        uint16_t held = 0;
        uint16_t names = compiler->local_count;
        uint16_t slots = compiler->next_slot;
        if (branch->binding.length > 0) {
            const KestType *optional = branch->condition->type;
            held = (uint16_t)(value_slots(optional) - 1);
            compiler->depth++;
            uint16_t slot = declare_local(
                compiler, branch->binding,
                optional == NULL ? NULL : optional->element);
            stack_pop(compiler, held);
            emit_store(compiler, slot, held, expr->span);
        }

        if (branch->then_value != NULL) {
            compile_expr(compiler, branch->then_value);
            // Both ways leave the same thing, so the depth after the `if` is
            // the depth after either one of them.
            stack_pop(compiler, gives);
        } else {
            compile_block(compiler, &branch->then_body);
        }

        if (branch->binding.length > 0) {
            compiler->depth--;
            compiler->local_count = names;
            compiler->next_slot = slots;
        }

        if (!branch->has_else && held == 0) {
            patch_exits(compiler, &otherwise, expr->span);
            break;
        }
        uint32_t done = emit_jump(compiler, KEST_OP_JUMP, expr->span);
        patch_exits(compiler, &otherwise, expr->span);
        if (held > 0) {
            emit(compiler, KEST_OP_POPN, expr->span);
            emit_u16(compiler, held, expr->span);
        }
        if (branch->otherwise != NULL) {
            compile_expr(compiler, branch->otherwise);
            stack_pop(compiler, gives);
        } else if (branch->else_value != NULL) {
            compile_expr(compiler, branch->else_value);
            stack_pop(compiler, gives);
        } else if (branch->has_else) {
            compile_block(compiler, &branch->else_body);
        }
        patch_jump(compiler, done, expr->span);
        stack_push(compiler, gives);
        break;
    }

    case KEST_EXPR_MATCH: {
        const KestChoose *choose = &expr->choose;
        uint32_t count = choose->subject_count;
        if (count > 8) {
            fault(compiler, expr->span,
                  "this matches more things at once than there are "
                  "instructions for");
            break;
        }
        const KestType *chosen[8];
        uint16_t subject[8];
        for (uint32_t i = 0; i < count; i++) {
            chosen[i] = choose->subjects[i]->type;
            if (chosen[i] == NULL || chosen[i]->tag != KEST_T_ENUM) {
                fault(compiler, expr->span, "this matches something that is "
                                            "not an enum");
                return;
            }
        }
        uint16_t gives = value_slots(expr->type);

        uint16_t names = compiler->local_count;
        uint16_t slots = compiler->next_slot;
        compiler->depth++;

        // Each subject goes into slots of its own, so an arm can name what a
        // case was carrying without moving anything.
        for (uint32_t i = 0; i < count; i++) {
            subject[i] = reserve_slot(compiler, chosen[i]->slots);
            compile_expr(compiler, choose->subjects[i]);
            stack_pop(compiler, chosen[i]->slots);
            emit_store(compiler, subject[i], chosen[i]->slots, expr->span);
        }

        uint32_t leaves[MAX_BREAKS];
        uint32_t leave_count = 0;
        for (uint32_t a = 0; a < choose->arm_count; a++) {
            const KestArm *arm = &choose->arms[a];
            bool blanket =
                arm->part_count == 1 && arm->parts[0].name.length == 0;

            // One test per position that names a case. A position that says
            // `else` tests nothing, so an arm of them tests nothing at all.
            uint32_t nexts[8];
            uint32_t next_count = 0;
            bool unknown = false;
            for (uint32_t p = 0; !blanket && p < arm->part_count && p < count;
                 p++) {
                const KestArmPart *part = &arm->parts[p];
                if (part->name.length == 0) {
                    continue;
                }
                const KestVariantType *variant =
                    case_named(chosen[p], span_text(compiler, part->name),
                               part->name.length);
                if (variant == NULL) {
                    unknown = true;
                    break;
                }
                stack_push(compiler, 1);
                emit_load(compiler, subject[p], 1, expr->span);
                KestValue tag = {0};
                tag.integer = (int64_t)(variant - chosen[p]->cases);
                emit_constant(compiler, tag, KEST_CONST_INT, expr->span);
                stack_pop(compiler, 1);
                emit(compiler, KEST_OP_EQ_I, expr->span);
                stack_pop(compiler, 1);
                nexts[next_count++] =
                    emit_jump(compiler, KEST_OP_JUMP_FALSE, expr->span);
            }
            if (unknown) {
                continue;
            }

            uint16_t arm_names = compiler->local_count;
            compiler->depth++;
            for (uint32_t p = 0; !blanket && p < arm->part_count && p < count;
                 p++) {
                const KestArmPart *part = &arm->parts[p];
                if (part->name.length == 0) {
                    continue;
                }
                const KestVariantType *variant =
                    case_named(chosen[p], span_text(compiler, part->name),
                               part->name.length);
                for (uint32_t b = 0;
                     b < part->binding_count && variant != NULL &&
                     b < variant->payload_count;
                     b++) {
                    bind_local(compiler, part->bindings[b],
                               (uint16_t)(subject[p] + variant->offsets[b]),
                               value_slots(variant->payload[b]));
                }
            }
            if (arm->value != NULL) {
                compile_expr(compiler, arm->value);
                // Every arm leaves the same thing, so the depth after the
                // match is the depth after any one of them.
                stack_pop(compiler, gives);
            } else {
                compile_block(compiler, &arm->body);
            }
            compiler->depth--;
            compiler->local_count = arm_names;

            if (leave_count < MAX_BREAKS) {
                leaves[leave_count++] =
                    emit_jump(compiler, KEST_OP_JUMP, expr->span);
            }
            for (uint32_t i = 0; i < next_count; i++) {
                patch_jump(compiler, nexts[i], expr->span);
            }
        }
        for (uint32_t i = 0; i < leave_count; i++) {
            patch_jump(compiler, leaves[i], expr->span);
        }
        stack_push(compiler, gives);

        compiler->depth--;
        compiler->local_count = names;
        compiler->next_slot = slots;
        break;
    }

    case KEST_EXPR_ARRAY: {
        const KestType *element =
            expr->type == NULL ? NULL : expr->type->element;
        // That many of something is the values themselves, one after another,
        // and nothing is built: they are already where they belong.
        if (expr->type != NULL && expr->type->tag == KEST_T_FIXED) {
            for (uint32_t i = 0; i < expr->array.count; i++) {
                compile_expr(compiler, expr->array.items[i]);
            }
            break;
        }
        uint16_t slots = value_slots(element);
        for (uint32_t i = 0; i < expr->array.count; i++) {
            compile_expr(compiler, expr->array.items[i]);
        }
        stack_pop(compiler, (uint16_t)(expr->array.count * slots));
        stack_push(compiler, 1);
        emit(compiler, KEST_OP_ARRAY, expr->span);
        emit_u16(compiler, (uint16_t)expr->array.count, expr->span);
        emit_u16(compiler, layout_of(compiler, element), expr->span);
        break;
    }
    }
}

static void compile_expr(Compiler *compiler, const KestExpr *expr) {
    if (expr == NULL || compiler->out_of_memory) {
        return;
    }
    compile_expr_kind(compiler, expr);
    // The checker decided this value stands where an optional is wanted, so
    // the tag goes after it.
    if (expr->wrapped) {
        stack_push(compiler, 1);
        emit(compiler, KEST_OP_TRUE, expr->span);
    }
}

static void compile_block(Compiler *compiler, const KestBlock *block);

static Loop *open_loop(Compiler *compiler, KestSpan span) {
    if (compiler->loop_count == MAX_LOOPS) {
        refuse(compiler, span, "K0502", "loops nest more than %d deep",
               MAX_LOOPS);
        return NULL;
    }
    Loop *loop = &compiler->loops[compiler->loop_count++];
    loop->start = compiler->chunk->code_count;
    loop->deferred = compiler->defer_count;
    loop->break_count = 0;
    loop->continue_count = 0;
    return loop;
}

// The pad every `continue` lands on sits between the body and the step, which
// is why continuing runs the step rather than skipping it.
static void land_continues(Compiler *compiler, Loop *loop, KestSpan span) {
    for (uint32_t i = 0; i < loop->continue_count; i++) {
        patch_jump(compiler, loop->continues[i], span);
    }
}

// Where the loop ends: the test that let it be skipped and every `break` land
// here, whatever went back at the bottom.
static void land_exit(Compiler *compiler, Loop *loop, const Exits *exits,
                      KestSpan span) {
    patch_exits(compiler, exits, span);
    for (uint32_t i = 0; i < loop->break_count; i++) {
        patch_jump(compiler, loop->breaks[i], span);
    }
    compiler->loop_count--;
}

static void finish_loop(Compiler *compiler, Loop *loop, const Exits *exits,
                        KestSpan span) {
    emit_loop(compiler, loop->start, span);
    land_exit(compiler, loop, exits, span);
}

static void close_loop(Compiler *compiler, Loop *loop, const Exits *exits,
                       KestSpan span) {
    land_continues(compiler, loop, span);
    finish_loop(compiler, loop, exits, span);
}

// What a walk keeps its place with. Every `for` in the language is this: a
// count in a slot, and either a limit beside it to count to or a store to look
// through. Which of the two is the only thing that differs between the five
// things a `for` can walk, so it is a field rather than four functions that
// agree because they were written in the same week.
typedef struct {
    uint16_t count;
    // Counting. The slot holding what the count is compared with.
    uint16_t limit;
    bool unsigned_count;
    // Looking. A store hands out slots that go dead, so there is no limit to
    // count to and the next live one is searched for.
    bool searching;
    uint16_t searched;
} Walk;

// The test that decides whether there is a first turn, written above the loop
// because every turn after it is decided at the bottom. Gives back where the
// way out is written, for `land_exit` to fill in.
static uint32_t open_walk(Compiler *compiler, Walk walk, KestSpan span) {
    if (walk.searching) {
        emit(compiler, KEST_OP_SEEK_FROM, span);
        emit_u16(compiler, walk.searched, span);
        emit_u16(compiler, walk.count, span);
        emit_u16(compiler, 0, span);
        return compiler->chunk->code_count - 2;
    }

    stack_push(compiler, 1);
    emit_load(compiler, walk.count, 1, span);
    stack_push(compiler, 1);
    emit_load(compiler, walk.limit, 1, span);
    stack_pop(compiler, 1);
    emit(compiler, walk.unsigned_count ? KEST_OP_LT_U : KEST_OP_LT_I, span);
    stack_pop(compiler, 1);
    return emit_jump(compiler, KEST_OP_JUMP_FALSE, span);
}

// The turn: one instruction that counts or looks, decides, and goes back while
// there is another. `continue` lands on it, which is why continuing takes a
// turn rather than skipping one.
static void close_walk(Compiler *compiler, Loop *loop, uint32_t exit, Walk walk,
                       KestSpan span) {
    land_continues(compiler, loop, span);

    if (walk.searching) {
        emit(compiler, KEST_OP_SEEK_NEXT, span);
        emit_u16(compiler, walk.searched, span);
        emit_u16(compiler, walk.count, span);
    } else {
        emit(compiler,
             walk.unsigned_count ? KEST_OP_NEXT_LESS_U : KEST_OP_NEXT_LESS_I,
             span);
        emit_u16(compiler, walk.count, span);
        emit_u16(compiler, walk.limit, span);
    }
    uint32_t distance = compiler->chunk->code_count + 2 - loop->start;
    if (distance > MAX_REACH) {
        refuse(compiler, span, "K0503",
               "this loop is %u bytes of code, and a loop reaches back %u",
               distance, (uint32_t)MAX_REACH);
        distance = 0;
    }
    emit_u16(compiler, (uint16_t)distance, span);

    Exits exits = one_exit(compiler, exit);
    land_exit(compiler, loop, &exits, span);
}

static void compile_stmt(Compiler *compiler, const KestStmt *stmt) {
    if (compiler->out_of_memory) {
        return;
    }

    switch (stmt->kind) {
    case KEST_STMT_LET: {
        compile_expr(compiler, stmt->let.value);
        const KestType *type = stmt->let.value == NULL
                                   ? NULL
                                   : stmt->let.value->type;
        uint16_t size = value_slots(type);
        uint16_t slot = declare_local(compiler, stmt->let.name, type);
        stack_pop(compiler, size);
        emit_store(compiler, slot, size == 0 ? 1 : size, stmt->span);
        break;
    }

    case KEST_STMT_ASSIGN: {
        const KestExpr *target = stmt->assign.target;
        uint16_t size = value_slots(target->type);

        // Writing one of that many, where the run is in slots. The index is
        // worked out while running, so it goes on the stack under the value.
        uint16_t written_slot = 0;
        uint16_t written_size = 0;
        bool at_a_slot = resolve_place(compiler, target, &written_slot,
                                       &written_size);
        if (!at_a_slot && target->kind == KEST_EXPR_INDEX &&
            target->index.object->type != NULL &&
            target->index.object->type->tag == KEST_T_FIXED) {
            const KestType *run = target->index.object->type;
            uint16_t base = 0;
            uint16_t run_size = 0;
            if (stmt->assign.op == KEST_TOK_EQ &&
                resolve_place(compiler, target->index.object, &base,
                              &run_size)) {
                compile_expr(compiler, target->index.index);
                compile_expr(compiler, stmt->assign.value);
                stack_pop(compiler, (uint16_t)(size + 1));
                emit(compiler, KEST_OP_STORE_SLOTS, stmt->span);
                emit_u16(compiler, base, stmt->span);
                emit_u16(compiler, size, stmt->span);
                emit_u16(compiler, (uint16_t)run->count, stmt->span);
                break;
            }
        }

        uint16_t slot = 0;
        uint16_t place_size = 0;
        bool in_slots = resolve_place(compiler, target, &slot, &place_size);

        uint16_t offset = 0;
        if (!in_slots && !compile_address(compiler, target, &offset)) {
            fault(compiler, target->span,
                  "this assigns to something that is not a place");
            break;
        }
        if (!in_slots) {
            stack_push(compiler, 1);
        }

        if (stmt->assign.op != KEST_TOK_EQ) {
            // The operator applies to what is there, so the target is read
            // before it is written. Through an address that means keeping a
            // second copy of it, because storing consumes one.
            if (in_slots) {
                stack_push(compiler, 1);
                emit_load(compiler, slot, 1, stmt->span);
            } else {
                stack_push(compiler, 1);
                emit(compiler, KEST_OP_DUP, stmt->span);
                stack_push(compiler, 1);
                stack_pop(compiler, 1);
                emit(compiler, KEST_OP_LOAD_AT, stmt->span);
                emit_u16(compiler, offset, stmt->span);
                emit_u16(compiler, layout_of(compiler, target->type),
                         stmt->span);
            }
        }

        compile_expr(compiler, stmt->assign.value);

        if (stmt->assign.op != KEST_TOK_EQ) {
            bool real = is_float(target->type);
            bool narrow = is_narrow(target->type);
            stack_pop(compiler, 1);
            switch (stmt->assign.op) {
            case KEST_TOK_PLUSEQ:
                emit(compiler,
                     real ? (narrow ? KEST_OP_ADD_F32 : KEST_OP_ADD_F)
                          : KEST_OP_ADD_I,
                     stmt->span);
                break;
            case KEST_TOK_MINUSEQ:
                emit(compiler,
                     real ? (narrow ? KEST_OP_SUB_F32 : KEST_OP_SUB_F)
                          : KEST_OP_SUB_I,
                     stmt->span);
                break;
            case KEST_TOK_STAREQ:
                emit(compiler,
                     real ? (narrow ? KEST_OP_MUL_F32 : KEST_OP_MUL_F)
                          : KEST_OP_MUL_I,
                     stmt->span);
                break;
            default:
                emit(compiler,
                     real ? (narrow ? KEST_OP_DIV_F32 : KEST_OP_DIV_F)
                          : (is_unsigned(target->type) ? KEST_OP_DIV_U
                                                       : KEST_OP_DIV_I),
                     stmt->span);
            }
            if (stmt->assign.op != KEST_TOK_SLASHEQ) {
                emit_narrow(compiler, target->type, stmt->span);
            }
        }

        if (in_slots) {
            stack_pop(compiler, size);
            emit_store(compiler, slot, size, stmt->span);
        } else {
            stack_pop(compiler, (uint16_t)(size + 1));
            emit(compiler, KEST_OP_STORE_AT, stmt->span);
            emit_u16(compiler, offset, stmt->span);
            emit_u16(compiler, layout_of(compiler, target->type), stmt->span);
        }
        break;
    }

    case KEST_STMT_EXPR:
        compile_expr(compiler, stmt->value);
        // A call that returns nothing left nothing behind to discard.
        {
            uint16_t size = stmt->value == NULL
                                ? 0
                                : value_slots(stmt->value->type);
            if (size == 1) {
                stack_pop(compiler, 1);
                emit(compiler, KEST_OP_POP, stmt->span);
            } else if (size > 1) {
                stack_pop(compiler, size);
                emit(compiler, KEST_OP_POPN, stmt->span);
                emit_u16(compiler, size, stmt->span);
            }
        }
        break;

    case KEST_STMT_WHILE: {
        uint16_t names = compiler->local_count;
        uint16_t slots = compiler->next_slot;
        bool opening = stmt->loop.binding.length > 0;
        if (opening) {
            compiler->depth++;
        }
        Loop *loop = open_loop(compiler, stmt->span);
        if (loop == NULL) {
            break;
        }
        Exits exit = compile_condition(compiler, stmt->loop.condition, opening);

        // `while let` leaves what the optional held below the tag the jump
        // consumed. The turn that ran binds it; the turn that stopped drops
        // it, which is why the way out is not where a `break` lands.
        uint16_t held = 0;
        if (opening) {
            const KestType *optional = stmt->loop.condition->type;
            held = (uint16_t)(value_slots(optional) - 1);
            uint16_t slot = declare_local(
                compiler, stmt->loop.binding,
                optional == NULL ? NULL : optional->element);
            stack_pop(compiler, held);
            emit_store(compiler, slot, held, stmt->span);
        }

        compile_block(compiler, &stmt->loop.body);

        if (held == 0) {
            close_loop(compiler, loop, &exit, stmt->span);
        } else {
            land_continues(compiler, loop, stmt->span);
            emit_loop(compiler, loop->start, stmt->span);
            patch_exits(compiler, &exit, stmt->span);
            emit(compiler, KEST_OP_POPN, stmt->span);
            emit_u16(compiler, held, stmt->span);
            for (uint32_t i = 0; i < loop->break_count; i++) {
                patch_jump(compiler, loop->breaks[i], stmt->span);
            }
            compiler->loop_count--;
        }

        if (opening) {
            compiler->depth--;
            compiler->local_count = names;
            compiler->next_slot = slots;
        }
        break;
    }

    case KEST_STMT_FOR: {
        // `for x in a` is a walk written here rather than in the parser, so
        // the counter and the thing being walked sit in slots nobody can name
        // or assign to.
        // `for i in from..to` counts. The end is worked out once and kept in
        // a slot nobody can name, so a call in it happens once rather than
        // every turn.
        if (stmt->each.until != NULL) {
            uint16_t names = compiler->local_count;
            uint16_t slots = compiler->next_slot;
            compiler->depth++;

            uint16_t end_slot = reserve_slot(compiler, 1);
            compile_expr(compiler, stmt->each.until);
            stack_pop(compiler, 1);
            emit_store(compiler, end_slot, 1, stmt->span);

            // The loop's own count stays where nobody can reach it and the
            // name is a copy of it, the same way a walk of an array works, so
            // assigning to that name cannot make the count go wrong.
            uint16_t index_slot = reserve_slot(compiler, 1);
            compile_expr(compiler, stmt->each.sequence);
            stack_pop(compiler, 1);
            emit_store(compiler, index_slot, 1, stmt->span);

            Walk walk = {index_slot, end_slot,
                         is_unsigned(stmt->each.sequence->type), false, 0};
            uint32_t exit = open_walk(compiler, walk, stmt->span);

            Loop *loop = open_loop(compiler, stmt->span);
            if (loop == NULL) {
                break;
            }

            uint16_t counter = declare_local(compiler, stmt->each.name,
                                             stmt->each.sequence->type);
            stack_push(compiler, 1);
            emit_load(compiler, index_slot, 1, stmt->span);
            stack_pop(compiler, 1);
            emit_store(compiler, counter, 1, stmt->span);

            compile_block(compiler, &stmt->each.body);
            close_walk(compiler, loop, exit, walk, stmt->span);

            compiler->depth--;
            compiler->local_count = names;
            compiler->next_slot = slots;
            break;
        }

        const KestType *sequence = stmt->each.sequence->type;
        bool over_store = sequence != NULL && sequence->tag == KEST_T_STORE;
        bool over_bits = sequence != NULL && sequence->tag == KEST_T_FLAGS;
        bool over_run = sequence != NULL && sequence->tag == KEST_T_FIXED;
        bool over_text = sequence != NULL && sequence->tag == KEST_T_TEXT;
        if (sequence == NULL ||
            (sequence->tag != KEST_T_ARRAY && !over_store && !over_bits &&
             !over_run && !over_text)) {
            fault(compiler, stmt->span,
                  "this walks something there is no walk for");
            break;
        }

        // That many of something is a value, so the walk is over a copy of
        // it. Walking it where it stands would let a write to it in the body
        // change what the walk reads, and D053 says the name is what the
        // element was when the turn began.
        if (over_run) {
            uint16_t stride = value_slots(sequence->element);
            uint16_t names = compiler->local_count;
            uint16_t slots = compiler->next_slot;
            compiler->depth++;

            uint16_t run_slot = reserve_slot(compiler, sequence->slots);
            compile_expr(compiler, stmt->each.sequence);
            stack_pop(compiler, sequence->slots);
            emit_store(compiler, run_slot, sequence->slots, stmt->span);

            uint16_t index_slot = reserve_slot(compiler, 1);
            KestValue zero = {0};
            emit_constant(compiler, zero, KEST_CONST_INT, stmt->span);
            stack_pop(compiler, 1);
            emit_store(compiler, index_slot, 1, stmt->span);

            // How many there are is written in the program, and it goes in a
            // slot beside the count anyway: a turn is then the one
            // instruction that counts and tests, the same as every other walk.
            uint16_t limit_slot = reserve_slot(compiler, 1);
            KestValue how_many = {0};
            how_many.integer = sequence->count;
            emit_constant(compiler, how_many, KEST_CONST_INT, stmt->span);
            stack_pop(compiler, 1);
            emit_store(compiler, limit_slot, 1, stmt->span);

            Walk walk = {index_slot, limit_slot, false, false, 0};
            uint32_t exit = open_walk(compiler, walk, stmt->span);

            Loop *loop = open_loop(compiler, stmt->span);
            if (loop == NULL) {
                break;
            }

            if (stmt->each.index.length > 0) {
                uint16_t named =
                    declare_local(compiler, stmt->each.index, NULL);
                stack_push(compiler, 1);
                emit_load(compiler, index_slot, 1, stmt->span);
                stack_pop(compiler, 1);
                emit_store(compiler, named, 1, stmt->span);
            }

            stack_push(compiler, 1);
            emit_load(compiler, index_slot, 1, stmt->span);
            stack_pop(compiler, 1);
            stack_push(compiler, stride);
            emit(compiler, KEST_OP_LOAD_SLOTS, stmt->span);
            emit_u16(compiler, run_slot, stmt->span);
            emit_u16(compiler, stride, stmt->span);
            emit_u16(compiler, (uint16_t)sequence->count, stmt->span);

            uint16_t held =
                declare_local(compiler, stmt->each.name, sequence->element);
            stack_pop(compiler, stride);
            emit_store(compiler, held, stride, stmt->span);

            compile_block(compiler, &stmt->each.body);
            close_walk(compiler, loop, exit, walk, stmt->span);

            compiler->depth--;
            compiler->local_count = names;
            compiler->next_slot = slots;
            break;
        }
        uint16_t stride = over_store || over_bits || over_text
                              ? 1
                              : value_slots(sequence->element);

        uint16_t names = compiler->local_count;
        uint16_t slots = compiler->next_slot;
        compiler->depth++;

        uint16_t walked_slot = reserve_slot(compiler, 1);
        uint16_t index_slot = reserve_slot(compiler, 1);

        compile_expr(compiler, stmt->each.sequence);
        stack_pop(compiler, 1);
        emit_store(compiler, walked_slot, 1, stmt->span);

        KestValue zero = {0};
        emit_constant(compiler, zero, KEST_CONST_INT, stmt->span);
        stack_pop(compiler, 1);
        emit_store(compiler, index_slot, 1, stmt->span);

        // What there is to walk, once. For an array that is how long it is:
        // a walk is over what it held when it began, so the body cannot
        // lengthen what it is walking by pushing to it. For a set of bits it
        // is how many names the set declares, which is known here. Either way
        // the limit is a slot beside the count, which is what makes a turn one
        // instruction. See D094.
        bool counted = !over_store;
        uint16_t limit_slot = 0;
        if (counted) {
            limit_slot = reserve_slot(compiler, 1);
            if (over_bits) {
                KestValue names_count = {0};
                names_count.integer = (int64_t)sequence->case_count;
                emit_constant(compiler, names_count, KEST_CONST_INT,
                              stmt->span);
            } else {
                stack_push(compiler, 1);
                emit_load(compiler, walked_slot, 1, stmt->span);
                emit(compiler, over_text ? KEST_OP_TEXT_LEN : KEST_OP_LEN,
                     stmt->span);
            }
            stack_pop(compiler, 1);
            emit_store(compiler, limit_slot, 1, stmt->span);
        }

        Walk walk = {index_slot, limit_slot, false, !counted, walked_slot};
        uint32_t before = open_walk(compiler, walk, stmt->span);

        Loop *loop = open_loop(compiler, stmt->span);
        if (loop == NULL) {
            break;
        }

        uint32_t exit = before;

        // The loop's own counter stays where nobody can reach it, and the
        // name the author asked for is a copy of it, so assigning to that
        // name cannot make the walk go wrong.
        if (stmt->each.index.length > 0) {
            uint16_t named = declare_local(compiler, stmt->each.index, NULL);
            stack_push(compiler, 1);
            emit_load(compiler, index_slot, 1, stmt->span);
            stack_pop(compiler, 1);
            emit_store(compiler, named, 1, stmt->span);
        }

        uint32_t absent = 0;
        if (over_bits) {
            // The flag this turn is about, which is the bit at the counter.
            KestValue one = {0};
            one.integer = 1;
            emit_constant(compiler, one, KEST_CONST_INT, stmt->span);
            stack_push(compiler, 1);
            emit_load(compiler, index_slot, 1, stmt->span);
            stack_pop(compiler, 1);
            emit(compiler, KEST_OP_SHL, stmt->span);

            // It goes into the name first, so a bit that is not there leaves
            // nothing on the stack to clean up on the way past.
            uint16_t held = declare_local(compiler, stmt->each.name, sequence);
            stack_pop(compiler, 1);
            emit_store(compiler, held, 1, stmt->span);

            stack_push(compiler, 1);
            emit_load(compiler, held, 1, stmt->span);
            stack_push(compiler, 1);
            emit_load(compiler, walked_slot, 1, stmt->span);
            stack_pop(compiler, 1);
            emit(compiler, KEST_OP_AND_I, stmt->span);
            stack_pop(compiler, 1);
            absent = emit_jump(compiler, KEST_OP_JUMP_FALSE, stmt->span);

            compile_block(compiler, &stmt->each.body);

            // A bit that is not set skips the body and lands on the step,
            // which is where `continue` lands too.
            patch_jump(compiler, absent, stmt->span);
            close_walk(compiler, loop, exit, walk, stmt->span);

            compiler->depth--;
            compiler->local_count = names;
            compiler->next_slot = slots;
            break;
        }

        // A body that only ever reads fields of the element does not need the
        // element: where it is, is enough, and the fields it does not read are
        // never touched.
        //
        // And it must not be able to write what it is walking. A copy is what
        // the element was when the turn began; an address is what it is now,
        // and the two differ the moment the body writes the array. Nothing
        // here can tell whether a call would write it, so the rule is that
        // the body does not name the thing being walked at all. See D053.
        const KestExpr *root = stmt->each.sequence;
        while (root != NULL && (root->kind == KEST_EXPR_FIELD ||
                                root->kind == KEST_EXPR_INDEX)) {
            root = root->kind == KEST_EXPR_FIELD ? root->field.object
                                                 : root->index.object;
        }
        bool untouched =
            root != NULL && root->kind == KEST_EXPR_NAME &&
            reads_only_fields(compiler, &stmt->each.body,
                              span_text(compiler, root->span),
                              root->span.length, false) &&
            writes_no_arrays(compiler, &stmt->each.body);
        bool by_address =
            !over_store && untouched && sequence->element != NULL &&
            sequence->element->tag == KEST_T_STRUCT &&
            reads_only_fields(compiler, &stmt->each.body,
                              span_text(compiler, stmt->each.name),
                              stmt->each.name.length, true);

        // The byte the walk is on. It reads the two slots itself rather than
        // taking them off the stack, because the walk measured the text when
        // it began and nothing it does can move a byte.
        if (over_text) {
            stack_push(compiler, 1);
            emit(compiler, KEST_OP_TEXT_IN, stmt->span);
            emit_u16(compiler, walked_slot, stmt->span);
            emit_u16(compiler, index_slot, stmt->span);
        } else {
            stack_push(compiler, 1);
            emit_load(compiler, walked_slot, 1, stmt->span);
            stack_push(compiler, 1);
            emit_load(compiler, index_slot, 1, stmt->span);
            stack_pop(compiler, 2);
            stack_push(compiler, by_address ? 1 : stride);
        }
        if (over_text) {
            // Read already.
        } else if (over_store) {
            emit(compiler, KEST_OP_STORE_REF, stmt->span);
        } else if (by_address) {
            emit(compiler, KEST_OP_ELEM_ADDR, stmt->span);
            emit_u16(compiler, layout_of(compiler, sequence->element),
                     stmt->span);
        } else {
            emit(compiler, KEST_OP_INDEX, stmt->span);
            emit_u16(compiler, layout_of(compiler, sequence->element),
                     stmt->span);
        }

        const KestType *bound =
            over_store || over_text ? NULL : sequence->element;
        uint16_t element_slot =
            declare_local(compiler, stmt->each.name, by_address ? NULL : bound);
        if (by_address) {
            Local *held = &compiler->locals[compiler->local_count - 1];
            held->is_address = true;
            held->points_at = bound;
        }
        stack_pop(compiler, by_address ? 1 : stride);
        emit_store(compiler, element_slot, by_address ? 1 : stride,
                   stmt->span);

        compile_block(compiler, &stmt->each.body);

        close_walk(compiler, loop, exit, walk, stmt->span);

        compiler->depth--;
        compiler->local_count = names;
        compiler->next_slot = slots;
        break;
    }

    case KEST_STMT_RETURN: {
        uint16_t size = 0;
        if (stmt->result != NULL) {
            compile_expr(compiler, stmt->result);
            size = value_slots(stmt->result->type);
            stack_pop(compiler, size);
        }
        // The answer is worked out first and then everything outstanding is
        // run, so what a deferred call sees is what the function decided.
        run_deferred(compiler, 0, stmt->span);
        emit(compiler, KEST_OP_RETURN, stmt->span);
        emit_u16(compiler, size, stmt->span);
        break;
    }

    case KEST_STMT_CONTINUE: {
        if (compiler->loop_count == 0) {
            break;
        }
        Loop *loop = &compiler->loops[compiler->loop_count - 1];
        run_deferred(compiler, loop->deferred, stmt->span);
        if (loop->continue_count == MAX_BREAKS) {
            refuse(compiler, stmt->span, "K0502",
                   "a loop holds at most %d continues", MAX_BREAKS);
            break;
        }
        loop->continues[loop->continue_count++] =
            emit_jump(compiler, KEST_OP_JUMP, stmt->span);
        break;
    }

    case KEST_STMT_BREAK: {
        if (compiler->loop_count == 0) {
            break;
        }
        Loop *loop = &compiler->loops[compiler->loop_count - 1];
        run_deferred(compiler, loop->deferred, stmt->span);
        if (loop->break_count == MAX_BREAKS) {
            refuse(compiler, stmt->span, "K0502",
                   "a loop holds at most %d breaks", MAX_BREAKS);
            break;
        }
        loop->breaks[loop->break_count++] =
            emit_jump(compiler, KEST_OP_JUMP, stmt->span);
        break;
    }

    case KEST_STMT_BLOCK:
        compile_block(compiler, &stmt->block);
        break;

    case KEST_STMT_DEFER:
        if (compiler->defer_count == MAX_DEFERS) {
            refuse(compiler, stmt->span, "K0502",
                   "a function defers at most %d things", MAX_DEFERS);
            break;
        }
        compiler->deferred[compiler->defer_count++] = stmt->value;
        break;
    }
}

// What was deferred since `from`, in reverse: the last thing deferred is the
// first thing undone, which is what everyone means by it.
// Whether a statement leaves the block rather than falling off the end of it.
static bool leaves_early(const KestStmt *stmt) {
    return stmt->kind == KEST_STMT_RETURN || stmt->kind == KEST_STMT_BREAK ||
           stmt->kind == KEST_STMT_CONTINUE;
}

static void run_deferred(Compiler *compiler, uint16_t from, KestSpan span) {
    for (uint16_t i = compiler->defer_count; i > from; i--) {
        const KestExpr *call = compiler->deferred[i - 1];
        compile_expr(compiler, call);
        uint16_t left = value_slots(call->type);
        if (left > 0) {
            stack_pop(compiler, left);
            emit(compiler, KEST_OP_POPN, span);
            emit_u16(compiler, left, span);
        }
    }
}

static void compile_block(Compiler *compiler, const KestBlock *block) {
    uint16_t names = compiler->local_count;
    uint16_t slots = compiler->next_slot;
    uint16_t defers = compiler->defer_count;
    compiler->depth++;
    for (uint32_t i = 0; i < block->count; i++) {
        compile_stmt(compiler, block->items[i]);
    }
    // On the way out of the block, unless the block already left through a
    // `return`, a `break` or a `continue`, each of which ran them itself.
    if (block->count == 0 || !leaves_early(block->items[block->count - 1])) {
        run_deferred(compiler, defers,
                     block->count > 0 ? block->items[block->count - 1]->span
                                      : (KestSpan){0, 0});
    }
    compiler->defer_count = defers;
    compiler->depth--;
    // Dropping the scope frees its slots for the next one, which is why two
    // sibling blocks do not each widen the frame.
    compiler->local_count = names;
    compiler->next_slot = slots;
}

static uint32_t unit_index(const KestUnits *units, const KestUnitInfo *unit) {
    for (uint32_t i = 0; i < units->count; i++) {
        if (&units->items[i] == unit) {
            return i;
        }
    }
    return 0;
}

// How wide each argument is, kept beside how wide they are together: a host
// filling a frame asks where the second one starts rather than working it out
// from the first one's fields.
static void remember_takes(Compiler *compiler, const KestType *signature) {
    if (signature == NULL) {
        return;
    }
    if (signature->result != NULL && signature->result->tag != KEST_T_VOID) {
        compiler->chunk->gives = layout_of(compiler, signature->result);
    }
    if (signature->param_count == 0) {
        return;
    }
    uint16_t *widths = KEST_ARENA_ARRAY(compiler->module->arena, uint16_t,
                                        signature->param_count);
    if (widths == NULL) {
        compiler->out_of_memory = true;
        return;
    }
    for (uint32_t p = 0; p < signature->param_count; p++) {
        widths[p] = layout_of(compiler, signature->params[p]);
    }
    compiler->chunk->takes = widths;
    compiler->chunk->takes_count = (uint16_t)signature->param_count;
}

// Two functions compiled under one name. Not a `fault` — that one takes a
// compiler, and this happens while the functions are being registered, before
// there is one — but the same kind of news, in the same words.
static void two_of_one_name(KestProgram *program, const char *symbol,
                            KestSpan where) {
    kest_diags_in(program->diags, program->source);
    kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0505", where,
                   "two functions are compiled under `%s`, which the checker "
                   "allowed",
                   symbol);
    kest_diags_fault(program->diags,
                     "the two halves of the compiler disagree about what a "
                     "program is");
}

bool kest_compile(KestProgram *program, const KestUnits *units,
                  KestModule *module) {
    Compiler compiler = {0};
    compiler.program = program;
    compiler.module = module;
    compiler.units = units;
    // The file that was named is the first one, and what it calls itself is
    // what a host has to be able to leave off.
    if (units->count > 0) {
        module->alias = units->items[0].alias;
    }

    // Every function in every file is registered before any body is emitted,
    // so a call can name one declared below it or in a file read later.
    for (uint32_t u = 0; u < units->count; u++) {
        kest_program_in(program, &units->items[u]);
        const KestUnit *unit = &units->items[u].unit;
        for (uint32_t i = 0; i < unit->count; i++) {
            const KestDecl *decl = unit->items[i];
            if (decl->kind != KEST_DECL_FN || decl->function.is_extern) {
                continue;
            }
            // Compiled under the symbol the checker gave it, which includes
            // what it takes, because two functions may share a name.
            KestSymbol *symbol =
                kest_symbol_at(program, program->source, decl->name);
            if (symbol == NULL || symbol->type->symbol == NULL) {
                continue;
            }
            // A generic function has no body of its own. Its copies are
            // registered below, one per set of types it was called with.
            if (symbol->type->type_param_count > 0) {
                continue;
            }
            KestChunk *chunk = kest_module_add(module, symbol->type->symbol);
            if (chunk == NULL) {
                two_of_one_name(program, symbol->type->symbol, decl->name);
                return false;
            }
            chunk->source = program->source;
            chunk->declared = symbol->span;
            chunk->returns_value = decl->function.result != NULL;
            chunk->result_slots = symbol->type->result == NULL
                                      ? 0
                                      : symbol->type->result->slots;
            chunk->no_alloc = symbol->type->no_alloc;
            chunk->param_slots = 0;
        }
    }

    for (uint32_t i = 0; i < program->instance_count; i++) {
        const KestInstance *instance = &program->instances[i];
        if (instance->symbol == NULL) {
            continue;
        }
        KestChunk *chunk = kest_module_add(module, instance->symbol);
        if (chunk == NULL) {
            kest_program_in(program, instance->unit);
            two_of_one_name(program, instance->symbol,
                            instance->decl->name);
            return false;
        }
        chunk->source = &instance->unit->source;
        // The generic's own declaration, which every copy of it shares: that
        // is what says the copies are copies rather than two functions of a
        // name. See D612.
        chunk->declared = instance->decl->name;
        chunk->returns_value = instance->decl->function.result != NULL;
        chunk->result_slots = instance->type->result == NULL
                                  ? 0
                                  : instance->type->result->slots;
        chunk->no_alloc = instance->type->no_alloc;
    }

    // Every constant is worked out here, where it is declared, rather than at
    // each use of it. Three things come of that: a constant read five times is
    // folded once, a constant read no times is still worked out — a program
    // could carry one that divides by nought and nothing said so — and what is
    // wrong with one is said at the declaration, which is where a reader looks
    // for what a name is.
    //
    // Walked a file at a time, because what a constant is written as is read
    // out of the file it is written in and a name in it may leave off the
    // module it is under. See D674.
    for (uint32_t u = 0; u < units->count; u++) {
        kest_program_in(program, &units->items[u]);
        kest_diags_in(program->diags, program->source);
        for (uint32_t i = 0; i < program->global_count; i++) {
            KestSymbol *symbol = &program->globals[i];
            if (!symbol->is_const || symbol->value == NULL ||
                symbol->type == NULL || symbol->source != program->source) {
                continue;
            }
            uint32_t slots = value_slots(symbol->type);
            KestValue *values = KEST_ARENA_ARRAY(program->arena, KestValue,
                                                 slots == 0 ? 1 : slots);
            if (values == NULL) {
                compiler.out_of_memory = true;
                return false;
            }
            const char *why = NULL;
            bool never = false;
            if (kest_fold_const(program, symbol->value, values, slots, &why,
                                &never) == slots) {
                symbol->folded = values;
                symbol->folded_slots = slots;
                continue;
            }
            symbol->would_not_fold = true;
            // Two refusals written out rather than one with a choice in it:
            // what a code can say is read out of this file, and a message
            // written under two codes at once is a wording neither of them
            // owns. See D673.
            if (never) {
                kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0510",
                               symbol->span,
                               "`%s` is made while running, so it is not a "
                               "constant",
                               symbol->name);
            } else {
                kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0504",
                               symbol->span,
                               "`%s` is not worked out where it is written",
                               symbol->name);
            }
            kest_diags_suggest(program->diags, "%s",
                               why != NULL
                                   ? why
                                   : "a constant is a number, a truth or a "
                                     "piece of text, and arithmetic on those "
                                     "and on other constants");
        }
    }

    uint32_t index = 0;
    for (uint32_t u = 0; u < units->count; u++) {
        kest_program_in(program, &units->items[u]);
        kest_diags_in(program->diags, program->source);
        const KestUnit *unit = &units->items[u].unit;

        for (uint32_t i = 0; i < unit->count; i++) {
            const KestDecl *decl = unit->items[i];
            if (decl->kind != KEST_DECL_FN || decl->function.is_extern) {
                continue;
            }
            KestSymbol *declared =
                kest_symbol_at(program, program->source, decl->name);
            if (declared != NULL && declared->type->type_param_count > 0) {
                continue;
            }

            compiler.chunk = module->functions[index++];
            compiler.unit = u;
            compiler.local_count = 0;
            compiler.next_slot = 0;
            compiler.slot_high_water = 0;
            compiler.stack_depth = 0;
            compiler.stack_high_water = 0;
            compiler.depth = 0;
            compiler.loop_count = 0;

            KestSymbol *symbol =
                kest_symbol_at(program, program->source, decl->name);
            for (uint32_t p = 0; p < decl->function.param_count; p++) {
                const KestType *type =
                    symbol != NULL && p < symbol->type->param_count
                        ? symbol->type->params[p]
                        : NULL;
                declare_local(&compiler, decl->function.params[p]->name, type);
            }
            compiler.chunk->param_slots = compiler.next_slot;
            remember_takes(&compiler, symbol == NULL ? NULL : symbol->type);

            compile_block(&compiler, &decl->function.body);
            emit(&compiler, KEST_OP_RETURN, decl->name);
            emit_u16(&compiler, 0, decl->name);

            compiler.chunk->slot_count = compiler.slot_high_water;
            compiler.chunk->stack_needed = compiler.stack_high_water;
        }
    }

    // Each copy of a generic function, compiled from the same body with its
    // type names bound. Nothing about it is a special case except that.
    for (uint32_t i = 0; i < program->instance_count; i++) {
        const KestInstance *instance = &program->instances[i];
        if (instance->symbol == NULL) {
            continue;
        }
        kest_program_in(program, (KestUnitInfo *)instance->unit);
        kest_diags_in(program->diags, program->source);
        KestInstance *made = &program->instances[i];
        if (!kest_retype_instance(program, made)) {
            return false;
        }
        kest_bind_types(program, made->names, made->bindings, made->count);

        compiler.chunk = module->functions[index++];
        compiler.unit = unit_index(units, instance->unit);
        compiler.local_count = 0;
        compiler.next_slot = 0;
        compiler.slot_high_water = 0;
        compiler.stack_depth = 0;
        compiler.stack_high_water = 0;
        compiler.depth = 0;
        compiler.loop_count = 0;

        const KestDecl *decl = instance->decl;
        for (uint32_t p = 0; p < decl->function.param_count; p++) {
            declare_local(&compiler, decl->function.params[p]->name,
                          p < instance->type->param_count
                              ? instance->type->params[p]
                              : NULL);
        }
        compiler.chunk->param_slots = compiler.next_slot;
        remember_takes(&compiler, instance->type);

        compile_block(&compiler, &decl->function.body);
        emit(&compiler, KEST_OP_RETURN, decl->name);
        emit_u16(&compiler, 0, decl->name);

        compiler.chunk->slot_count = compiler.slot_high_water;
        compiler.chunk->stack_needed = compiler.stack_high_water;
        kest_unbind_types(program);
    }

    // Every element type a signature mentions gets a layout, whether or not a
    // body ever reached one. What a host can be handed is what the program
    // says it takes, and that is written in the declarations rather than in
    // what the bodies happened to compile to. See D068.
    for (uint32_t i = 0; i < program->global_count; i++) {
        const KestType *type = program->globals[i].type;
        if (type == NULL || type->tag != KEST_T_FN) {
            continue;
        }
        for (uint32_t p = 0; p <= type->param_count; p++) {
            const KestType *held =
                p == type->param_count ? type->result : type->params[p];
            // An array only. A store is a slot map with generations and a
            // free list, so nothing a host has is one, and saying its element
            // could be lent would be offering something with nowhere to go.
            if (held == NULL || held->tag != KEST_T_ARRAY) {
                continue;
            }
            if (kest_module_layout(module, held->element) < 0) {
                return false;
            }
        }
    }

    return !compiler.out_of_memory;
}
