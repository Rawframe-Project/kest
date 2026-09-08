#include "compile.h"

#include "check.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LOCALS 256
#define MAX_LOOPS 16
#define MAX_BREAKS 32

typedef struct {
    const char *name;
    uint16_t slot;
    // A struct value occupies a run of slots, so a name is a place and a
    // width rather than a single index.
    uint16_t size;
    uint32_t depth;
} Local;

typedef struct {
    uint32_t start;
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
    char message[512];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    kest_diags_add(compiler->program->diags, KEST_SEVERITY_ERROR, code, span,
                   "%s", message);
    compiler->failed = true;
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
static uint32_t emit_jump(Compiler *compiler, uint8_t op, KestSpan origin) {
    emit(compiler, op, origin);
    emit_u16(compiler, 0, origin);
    return compiler->chunk->code_count - 2;
}

static void patch_jump(Compiler *compiler, uint32_t placeholder,
                       KestSpan origin) {
    uint32_t distance = compiler->chunk->code_count - placeholder - 2;
    if (distance > UINT16_MAX) {
        refuse(compiler, origin, "K0503", "this jumps too far to encode");
        return;
    }
    compiler->chunk->code[placeholder] = (uint8_t)(distance & 0xff);
    compiler->chunk->code[placeholder + 1] = (uint8_t)(distance >> 8);
}

static void emit_loop(Compiler *compiler, uint32_t start, KestSpan origin) {
    emit(compiler, KEST_OP_LOOP, origin);
    uint32_t distance = compiler->chunk->code_count + 2 - start;
    if (distance > UINT16_MAX) {
        refuse(compiler, origin, "K0503", "this loop is too long to encode");
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
    local->name = kest_arena_strndup(compiler->program->arena,
                                     span_text(compiler, span), span.length);
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
    Local *local = &compiler->locals[compiler->local_count++];
    local->name = kest_arena_strndup(compiler->program->arena,
                                     span_text(compiler, span), span.length);
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
static bool resolve_place(Compiler *compiler, const KestExpr *expr,
                          uint16_t *slot, uint16_t *size) {
    if (expr->kind == KEST_EXPR_NAME) {
        Local *local = find_local(compiler, expr->span);
        if (local == NULL) {
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
static const char *literal_text(Compiler *compiler, KestSpan span) {
    const char *raw = span_text(compiler, span);
    size_t length = span.length;

    char *text = kest_arena_alloc(compiler->program->arena, length + 1, 1);
    if (text == NULL) {
        compiler->out_of_memory = true;
        return "";
    }

    size_t used = 0;
    for (size_t i = 0; i < length; i++) {
        if (raw[i] != '\\' || i + 1 == length) {
            text[used++] = raw[i];
            continue;
        }
        i++;
        switch (raw[i]) {
        case 'n':
            text[used++] = '\n';
            break;
        case 't':
            text[used++] = '\t';
            break;
        case 'r':
            text[used++] = '\r';
            break;
        case '0':
            text[used++] = '\0';
            break;
        default:
            text[used++] = raw[i];
        }
    }
    text[used] = '\0';
    return text;
}

static double parse_real(Compiler *compiler, KestSpan span) {
    char buffer[64];
    size_t length = span.length < sizeof(buffer) - 1 ? span.length : 0;
    memcpy(buffer, span_text(compiler, span), length);
    buffer[length] = '\0';
    return strtod(buffer, NULL);
}

static void compile_expr(Compiler *compiler, const KestExpr *expr);
static void compile_block(Compiler *compiler, const KestBlock *block);
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
        refuse(compiler, expr->span, "K0501",
               "`%.*s` is not a function this can name",
               (int)expr->span.length, span_text(compiler, expr->span));
        return true;
    }
    KestValue which = {0};
    which.integer = index;
    stack_push(compiler, 1);
    emit_constant(compiler, which, KEST_CONST_INT, expr->span);
    return true;
}

static void compile_constant(Compiler *compiler, const KestExpr *expr) {
    const char *name = span_text(compiler, expr->span);

    if (compile_function_value(compiler, expr)) {
        return;
    }

    const KestUnit *unit = &compiler->units->items[compiler->unit].unit;
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_CONST ||
            decl->name.length != expr->span.length ||
            memcmp(compiler->program->source->text + decl->name.offset, name,
                   expr->span.length) != 0) {
            continue;
        }

        const KestExpr *value = decl->constant.value;
        const KestExpr *literal = value;
        if (literal != NULL && literal->kind == KEST_EXPR_UNARY) {
            literal = literal->unary.operand;
        }
        if (literal == NULL || (literal->kind != KEST_EXPR_INT &&
                                literal->kind != KEST_EXPR_FLOAT &&
                                literal->kind != KEST_EXPR_STRING &&
                                literal->kind != KEST_EXPR_BOOL)) {
            refuse(compiler, expr->span, "K0501",
                   "only a literal constant is compiled yet");
            return;
        }
        compile_expr(compiler, value);
        return;
    }

    refuse(compiler, expr->span, "K0501", "`%.*s` cannot be reached yet",
           (int)expr->span.length, name);
}

// Whether an address can be worked out for this, asked before anything is
// emitted. `compile_address` emits as it goes, so a caller that has somewhere
// else to fall back to has to know beforehand rather than find out halfway.
static bool can_address(Compiler *compiler, const KestExpr *expr) {
    if (expr->kind == KEST_EXPR_FIELD) {
        return can_address(compiler, expr->field.object) &&
               find_member(expr->field.object->type,
                           span_text(compiler, expr->field.name),
                           expr->field.name.length) != NULL;
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

    switch (op) {
    case KEST_TOK_PLUS:
        emit(compiler, real ? (narrow ? KEST_OP_ADD_F32 : KEST_OP_ADD_F)
                    : KEST_OP_ADD_I,
             span);
        break;
    case KEST_TOK_MINUS:
        emit(compiler, real ? (narrow ? KEST_OP_SUB_F32 : KEST_OP_SUB_F)
                    : KEST_OP_SUB_I,
             span);
        break;
    case KEST_TOK_STAR:
        emit(compiler, real ? (narrow ? KEST_OP_MUL_F32 : KEST_OP_MUL_F)
                    : KEST_OP_MUL_I,
             span);
        break;
    case KEST_TOK_SLASH:
        emit(compiler,
             real ? (narrow ? KEST_OP_DIV_F32 : KEST_OP_DIV_F)
                  : (unsigned_int ? KEST_OP_DIV_U : KEST_OP_DIV_I),
             span);
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
        break;
    case KEST_TOK_GTGT:
        // What shifts in on the right is the sign when there is one, and
        // nought when there is not, which is what the two types mean.
        emit(compiler, unsigned_int ? KEST_OP_SHR_U : KEST_OP_SHR_I, span);
        break;
    case KEST_TOK_LT:
        emit(compiler,
             text ? KEST_OP_LT_T
                  : real ? KEST_OP_LT_F
                         : (unsigned_int ? KEST_OP_LT_U : KEST_OP_LT_I),
             span);
        break;
    case KEST_TOK_LTEQ:
        emit(compiler,
             text ? KEST_OP_LE_T
                  : real ? KEST_OP_LE_F
                         : (unsigned_int ? KEST_OP_LE_U : KEST_OP_LE_I),
             span);
        break;
    case KEST_TOK_GT:
        emit(compiler,
             text ? KEST_OP_GT_T
                  : real ? KEST_OP_GT_F
                         : (unsigned_int ? KEST_OP_GT_U : KEST_OP_GT_I),
             span);
        break;
    case KEST_TOK_GTEQ:
        emit(compiler,
             text ? KEST_OP_GE_T
                  : real ? KEST_OP_GE_F
                         : (unsigned_int ? KEST_OP_GE_U : KEST_OP_GE_I),
             span);
        break;
    case KEST_TOK_EQEQ:
        if (operand != NULL && operand->tag == KEST_T_ENUM) {
            // Both are a run of slots and the answer is one, so the depth
            // after is one below where a scalar compare would leave it.
            stack_pop(compiler, (uint16_t)((operand->slots - 1) * 2));
            emit(compiler, KEST_OP_EQ_ENUM, span);
            emit_u16(compiler, layout_of(compiler, operand), span);
            break;
        }
        emit(compiler,
             real ? KEST_OP_EQ_F
                  : (operand != NULL && operand->tag == KEST_T_TEXT
                         ? KEST_OP_EQ_T
                         : KEST_OP_EQ_I),
             span);
        break;
    case KEST_TOK_BANGEQ:
        if (operand != NULL && operand->tag == KEST_T_ENUM) {
            stack_pop(compiler, (uint16_t)((operand->slots - 1) * 2));
            emit(compiler, KEST_OP_NE_ENUM, span);
            emit_u16(compiler, layout_of(compiler, operand), span);
            break;
        }
        emit(compiler,
             real ? KEST_OP_NE_F
                  : (operand != NULL && operand->tag == KEST_T_TEXT
                         ? KEST_OP_NE_T
                         : KEST_OP_NE_I),
             span);
        break;
    default:
        refuse(compiler, span, "K0501", "this operator is not compiled yet");
        return;
    }

    // What can leave the declared width has to come back to it (D018). `&`,
    // `|`, `^` and `>>` cannot: every bit they produce was already in range.
    switch (op) {
    case KEST_TOK_PLUS:
    case KEST_TOK_MINUS:
    case KEST_TOK_STAR:
    case KEST_TOK_LTLT:
        emit_narrow(compiler, operand, span);
        break;
    default:
        break;
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

    if (builtin_named(compiler, name, length, "find")) {
        stack_pop(compiler, 2);
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

static void compile_call(Compiler *compiler, const KestExpr *expr) {
    const KestExpr *callee = expr->call.callee;
    // A dotted callee is a function in another module, or an extern named for
    // its host type. Both are one name with a dot in it.
    if (callee->kind != KEST_EXPR_NAME && callee->kind != KEST_EXPR_FIELD) {
        refuse(compiler, callee->span, "K0501",
               "only a named function can be called so far");
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
        compile_expr(compiler, callee);
        uint16_t through = 0;
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            through += value_slots(expr->call.args[i]->type);
        }
        stack_pop(compiler, (uint16_t)(through + 1));
        stack_push(compiler, value_slots(expr->type));
        emit(compiler, KEST_OP_CALL_VALUE, expr->span);
        emit_u16(compiler, through, expr->span);
        return;
    }

    const char *name = span_text(compiler, callee->span);
    uint16_t argument_slots = 0;
    for (uint32_t i = 0; i < expr->call.arg_count; i++) {
        argument_slots += value_slots(expr->call.args[i]->type);
    }
    uint16_t result_slots = value_slots(expr->type);

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
        refuse(compiler, callee->span, "K0501", "`%.*s` has no body to call",
               (int)callee->span.length, name);
        return;
    }

    const KestSymbol *declared =
        kest_lookup_global(compiler->program, name, callee->span.length);
    int32_t slot = kest_module_extern(
        compiler->module, foreign->foreign_name,
        declared == NULL ? callee->span : declared->span,
        declared == NULL ? compiler->program->source : declared->source);
    if (slot < 0) {
        compiler->out_of_memory = true;
        return;
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
        // `sort.ascending` is one name with a dot in it, not a field of a
        // `sort`, and where a value is wanted it is which function it is.
        if (compile_function_value(compiler, expr)) {
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
            refuse(compiler, expr->span, "K0501",
                   "this field cannot be reached yet");
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
        const KestType *object = expr->index.object->type;
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
            if (type->tag == KEST_T_FLAGS || type->tag == KEST_T_ENUM) {
                // An enum is a run of slots and its text is one, so what the
                // walk of the parts counts has to come back to one.
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

        compile_expr(compiler, branch->condition);
        stack_pop(compiler, 1);
        uint32_t otherwise =
            emit_jump(compiler, KEST_OP_JUMP_FALSE, expr->span);

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
            patch_jump(compiler, otherwise, expr->span);
            break;
        }
        uint32_t done = emit_jump(compiler, KEST_OP_JUMP, expr->span);
        patch_jump(compiler, otherwise, expr->span);
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
            refuse(compiler, expr->span, "K0501",
                   "`match` chooses between at most 8 things");
            break;
        }
        const KestType *chosen[8];
        uint16_t subject[8];
        for (uint32_t i = 0; i < count; i++) {
            chosen[i] = choose->subjects[i]->type;
            if (chosen[i] == NULL || chosen[i]->tag != KEST_T_ENUM) {
                refuse(compiler, expr->span, "K0501",
                       "`match` chooses an enum");
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

static void finish_loop(Compiler *compiler, Loop *loop, uint32_t exit,
                        KestSpan span) {
    emit_loop(compiler, loop->start, span);
    patch_jump(compiler, exit, span);
    for (uint32_t i = 0; i < loop->break_count; i++) {
        patch_jump(compiler, loop->breaks[i], span);
    }
    compiler->loop_count--;
}

static void close_loop(Compiler *compiler, Loop *loop, uint32_t exit,
                       KestSpan span) {
    land_continues(compiler, loop, span);
    finish_loop(compiler, loop, exit, span);
}

static void close_loop_with_step(Compiler *compiler, Loop *loop, uint32_t exit,
                                 uint16_t index_slot, KestSpan span) {
    land_continues(compiler, loop, span);

    stack_push(compiler, 1);
    emit_load(compiler, index_slot, 1, span);
    KestValue one = {0};
    one.integer = 1;
    emit_constant(compiler, one, KEST_CONST_INT, span);
    stack_pop(compiler, 1);
    emit(compiler, KEST_OP_ADD_I, span);
    stack_pop(compiler, 1);
    emit_store(compiler, index_slot, 1, span);

    finish_loop(compiler, loop, exit, span);
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

        uint16_t slot = 0;
        uint16_t place_size = 0;
        bool in_slots = resolve_place(compiler, target, &slot, &place_size);

        uint16_t offset = 0;
        if (!in_slots && !compile_address(compiler, target, &offset)) {
            refuse(compiler, target->span, "K0501",
                   "this cannot be assigned to yet");
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
        Loop *loop = open_loop(compiler, stmt->span);
        if (loop == NULL) {
            break;
        }
        compile_expr(compiler, stmt->loop.condition);
        stack_pop(compiler, 1);
        uint32_t exit = emit_jump(compiler, KEST_OP_JUMP_FALSE, stmt->span);
        compile_block(compiler, &stmt->loop.body);
        close_loop(compiler, loop, exit, stmt->span);
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

            Loop *loop = open_loop(compiler, stmt->span);
            if (loop == NULL) {
                break;
            }
            stack_push(compiler, 1);
            emit_load(compiler, index_slot, 1, stmt->span);
            stack_push(compiler, 1);
            emit_load(compiler, end_slot, 1, stmt->span);
            stack_pop(compiler, 1);
            emit(compiler,
                 is_unsigned(stmt->each.sequence->type) ? KEST_OP_LT_U
                                                        : KEST_OP_LT_I,
                 stmt->span);
            stack_pop(compiler, 1);
            uint32_t exit = emit_jump(compiler, KEST_OP_JUMP_FALSE, stmt->span);

            uint16_t counter = declare_local(compiler, stmt->each.name,
                                             stmt->each.sequence->type);
            stack_push(compiler, 1);
            emit_load(compiler, index_slot, 1, stmt->span);
            stack_pop(compiler, 1);
            emit_store(compiler, counter, 1, stmt->span);

            compile_block(compiler, &stmt->each.body);
            close_loop_with_step(compiler, loop, exit, index_slot, stmt->span);

            compiler->depth--;
            compiler->local_count = names;
            compiler->next_slot = slots;
            break;
        }

        const KestType *sequence = stmt->each.sequence->type;
        bool over_store = sequence != NULL && sequence->tag == KEST_T_STORE;
        bool over_bits = sequence != NULL && sequence->tag == KEST_T_FLAGS;
        if (sequence == NULL ||
            (sequence->tag != KEST_T_ARRAY && !over_store && !over_bits)) {
            refuse(compiler, stmt->span, "K0501",
                   "`for` walks an array, a store or a set of bits");
            break;
        }
        uint16_t stride =
            over_store || over_bits ? 1 : value_slots(sequence->element);

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

        Loop *loop = open_loop(compiler, stmt->span);
        if (loop == NULL) {
            break;
        }

        uint32_t exit;
        if (over_bits) {
            // Every bit the set declares is looked at, and the ones that are
            // not there are stepped over. A set has as many bits as it has
            // names, so the end is known when this is compiled.
            stack_push(compiler, 1);
            emit_load(compiler, index_slot, 1, stmt->span);
            KestValue names_count = {0};
            names_count.integer = (int64_t)sequence->case_count;
            emit_constant(compiler, names_count, KEST_CONST_INT, stmt->span);
            stack_pop(compiler, 1);
            emit(compiler, KEST_OP_LT_I, stmt->span);
        } else if (over_store) {
            // Slots go dead, so the next one is looked for rather than
            // counted to, and where the search stopped is where it resumes.
            stack_push(compiler, 1);
            emit_load(compiler, walked_slot, 1, stmt->span);
            stack_push(compiler, 1);
            emit_load(compiler, index_slot, 1, stmt->span);
            stack_pop(compiler, 1);
            emit(compiler, KEST_OP_SEEK, stmt->span);
            stack_pop(compiler, 1);
            emit_store(compiler, index_slot, 1, stmt->span);

            stack_push(compiler, 1);
            emit_load(compiler, index_slot, 1, stmt->span);
            emit_constant(compiler, zero, KEST_CONST_INT, stmt->span);
            stack_pop(compiler, 1);
            emit(compiler, KEST_OP_GE_I, stmt->span);
        } else {
            stack_push(compiler, 1);
            emit_load(compiler, index_slot, 1, stmt->span);
            stack_push(compiler, 1);
            emit_load(compiler, walked_slot, 1, stmt->span);
            emit(compiler, KEST_OP_LEN, stmt->span);
            stack_pop(compiler, 1);
            emit(compiler, KEST_OP_LT_I, stmt->span);
        }
        stack_pop(compiler, 1);
        exit = emit_jump(compiler, KEST_OP_JUMP_FALSE, stmt->span);

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
            close_loop_with_step(compiler, loop, exit, index_slot, stmt->span);

            compiler->depth--;
            compiler->local_count = names;
            compiler->next_slot = slots;
            break;
        }

        stack_push(compiler, 1);
        emit_load(compiler, walked_slot, 1, stmt->span);
        stack_push(compiler, 1);
        emit_load(compiler, index_slot, 1, stmt->span);
        stack_pop(compiler, 2);
        stack_push(compiler, stride);
        if (over_store) {
            emit(compiler, KEST_OP_STORE_REF, stmt->span);
        } else {
            emit(compiler, KEST_OP_INDEX, stmt->span);
            emit_u16(compiler, layout_of(compiler, sequence->element),
                     stmt->span);
        }

        const KestType *bound =
            over_store ? NULL : sequence->element;
        uint16_t element_slot =
            declare_local(compiler, stmt->each.name, bound);
        stack_pop(compiler, stride);
        emit_store(compiler, element_slot, stride, stmt->span);

        compile_block(compiler, &stmt->each.body);

        close_loop_with_step(compiler, loop, exit, index_slot, stmt->span);

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
        emit(compiler, KEST_OP_RETURN, stmt->span);
        emit_u16(compiler, size, stmt->span);
        break;
    }

    case KEST_STMT_CONTINUE: {
        if (compiler->loop_count == 0) {
            break;
        }
        Loop *loop = &compiler->loops[compiler->loop_count - 1];
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
    }
}

static void compile_block(Compiler *compiler, const KestBlock *block) {
    uint16_t names = compiler->local_count;
    uint16_t slots = compiler->next_slot;
    compiler->depth++;
    for (uint32_t i = 0; i < block->count; i++) {
        compile_stmt(compiler, block->items[i]);
    }
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

bool kest_compile(KestProgram *program, const KestUnits *units,
                  KestModule *module) {
    Compiler compiler = {0};
    compiler.program = program;
    compiler.module = module;
    compiler.units = units;

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
                return false;
            }
            chunk->source = program->source;
            chunk->returns_value = decl->function.result != NULL;
        }
    }

    for (uint32_t i = 0; i < program->instance_count; i++) {
        const KestInstance *instance = &program->instances[i];
        if (instance->symbol == NULL) {
            continue;
        }
        KestChunk *chunk = kest_module_add(module, instance->symbol);
        if (chunk == NULL) {
            return false;
        }
        chunk->source = &instance->unit->source;
        chunk->returns_value = instance->decl->function.result != NULL;
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

        compile_block(&compiler, &decl->function.body);
        emit(&compiler, KEST_OP_RETURN, decl->name);
        emit_u16(&compiler, 0, decl->name);

        compiler.chunk->slot_count = compiler.slot_high_water;
        compiler.chunk->stack_needed = compiler.stack_high_water;
        kest_unbind_types(program);
    }

    return !compiler.out_of_memory;
}
