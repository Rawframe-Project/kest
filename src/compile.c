#include "compile.h"

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
    if (type == NULL || type->tag != KEST_T_INT || type->width == 64) {
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

// A constant is written into every use of it rather than loaded, which is
// what makes it a constant rather than a variable nobody assigns to.
static void compile_constant(Compiler *compiler, const KestExpr *expr) {
    const char *name = span_text(compiler, expr->span);
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
    case KEST_TOK_LT:
        emit(compiler,
             real ? KEST_OP_LT_F : (unsigned_int ? KEST_OP_LT_U : KEST_OP_LT_I),
             span);
        break;
    case KEST_TOK_LTEQ:
        emit(compiler,
             real ? KEST_OP_LE_F : (unsigned_int ? KEST_OP_LE_U : KEST_OP_LE_I),
             span);
        break;
    case KEST_TOK_GT:
        emit(compiler,
             real ? KEST_OP_GT_F : (unsigned_int ? KEST_OP_GT_U : KEST_OP_GT_I),
             span);
        break;
    case KEST_TOK_GTEQ:
        emit(compiler,
             real ? KEST_OP_GE_F : (unsigned_int ? KEST_OP_GE_U : KEST_OP_GE_I),
             span);
        break;
    case KEST_TOK_EQEQ:
        emit(compiler,
             real ? KEST_OP_EQ_F
                  : (operand != NULL && operand->tag == KEST_T_TEXT
                         ? KEST_OP_EQ_T
                         : KEST_OP_EQ_I),
             span);
        break;
    case KEST_TOK_BANGEQ:
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

    switch (op) {
    case KEST_TOK_PLUS:
    case KEST_TOK_MINUS:
    case KEST_TOK_STAR:
        emit_narrow(compiler, operand, span);
        break;
    default:
        break;
    }
}

// A function as the file being compiled writes it: its own bare, everything
// else prefixed with the module it came from.
static int32_t find_chunk(Compiler *compiler, const char *name,
                          size_t length) {
    const char *alias = compiler->program->alias;
    char joined[256];
    if (alias[0] != '\0') {
        int written = snprintf(joined, sizeof(joined), "%s.%.*s", alias,
                               (int)length, name);
        if (written > 0 && (size_t)written < sizeof(joined)) {
            int32_t found = kest_module_find(compiler->module, joined);
            if (found >= 0) {
                return found;
            }
        }
    }
    int written = snprintf(joined, sizeof(joined), "%.*s", (int)length, name);
    if (written <= 0 || (size_t)written >= sizeof(joined)) {
        return -1;
    }
    return kest_module_find(compiler->module, joined);
}

static bool builtin_named(Compiler *compiler, const char *name, size_t length,
                          const char *word) {
    return strlen(word) == length && memcmp(name, word, length) == 0 &&
           find_chunk(compiler, word, length) < 0;
}

// The arguments are already on the stack in the order they were written, so
// each of these is one instruction over them.
static bool compile_builtin(Compiler *compiler, const KestExpr *expr,
                            const char *name, size_t length) {
    if (builtin_named(compiler, name, length, "len")) {
        const KestType *subject =
            expr->call.arg_count > 0 ? expr->call.args[0]->type : NULL;
        bool store = subject != NULL && subject->tag == KEST_T_STORE;
        emit(compiler, store ? KEST_OP_COUNT : KEST_OP_LEN, expr->span);
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
    if (callee->type != NULL && (callee->type->tag == KEST_T_INT ||
                                 callee->type->tag == KEST_T_FLOAT)) {
        compile_conversion(compiler, expr, callee->type);
        return;
    }

    const char *name = span_text(compiler, callee->span);
    if (callee->span.length == 5 && memcmp(name, "print", 5) == 0) {
        stack_pop(compiler, 1);
        emit(compiler, KEST_OP_PRINT, expr->span);
        return;
    }
    if (compile_builtin(compiler, expr, name, callee->span.length)) {
        return;
    }

    uint16_t argument_slots = 0;
    for (uint32_t i = 0; i < expr->call.arg_count; i++) {
        argument_slots += value_slots(expr->call.args[i]->type);
    }
    uint16_t result_slots = value_slots(expr->type);

    int32_t index = find_chunk(compiler, name, callee->span.length);
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
    KestSymbol *foreign =
        kest_lookup_global(compiler->program, name, callee->span.length);
    if (foreign == NULL || foreign->type->tag != KEST_T_FN ||
        !foreign->type->is_foreign) {
        refuse(compiler, callee->span, "K0501", "`%.*s` has no body to call",
               (int)callee->span.length, name);
        return;
    }

    int32_t slot =
        kest_module_extern(compiler->module, foreign->type->foreign_name,
                           foreign->span, foreign->source);
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
        uint16_t slot = 0;
        uint16_t size = 0;
        if (resolve_place(compiler, expr, &slot, &size)) {
            stack_push(compiler, size);
            emit_load(compiler, slot, size, expr->span);
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
        compile_expr(compiler, expr->index.object);
        compile_expr(compiler, expr->index.index);
        stack_pop(compiler, 2);
        stack_push(compiler, value_slots(expr->type));
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

    case KEST_STMT_IF: {
        compile_expr(compiler, stmt->branch.condition);
        stack_pop(compiler, 1);
        uint32_t otherwise =
            emit_jump(compiler, KEST_OP_JUMP_FALSE, stmt->span);

        // `if let` leaves what the optional held below the tag the jump
        // consumed. The taken arm binds it; the other arm drops it.
        uint16_t held = 0;
        uint16_t names = compiler->local_count;
        uint16_t slots = compiler->next_slot;
        if (stmt->branch.binding.length > 0) {
            const KestType *optional = stmt->branch.condition->type;
            held = (uint16_t)(value_slots(optional) - 1);
            compiler->depth++;
            uint16_t slot = declare_local(compiler, stmt->branch.binding,
                                          optional == NULL ? NULL
                                                           : optional->element);
            stack_pop(compiler, held);
            emit_store(compiler, slot, held, stmt->span);
        }

        compile_block(compiler, &stmt->branch.then_body);

        if (stmt->branch.binding.length > 0) {
            compiler->depth--;
            compiler->local_count = names;
            compiler->next_slot = slots;
        }

        if (stmt->branch.otherwise == NULL && held == 0) {
            patch_jump(compiler, otherwise, stmt->span);
            break;
        }
        uint32_t done = emit_jump(compiler, KEST_OP_JUMP, stmt->span);
        patch_jump(compiler, otherwise, stmt->span);
        if (held > 0) {
            emit(compiler, KEST_OP_POPN, stmt->span);
            emit_u16(compiler, held, stmt->span);
        }
        if (stmt->branch.otherwise != NULL) {
            compile_stmt(compiler, stmt->branch.otherwise);
        }
        patch_jump(compiler, done, stmt->span);
        break;
    }

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
        const KestType *sequence = stmt->each.sequence->type;
        bool over_store = sequence != NULL && sequence->tag == KEST_T_STORE;
        if (sequence == NULL ||
            (sequence->tag != KEST_T_ARRAY && !over_store)) {
            refuse(compiler, stmt->span, "K0501",
                   "`for` walks an array or a store");
            break;
        }
        uint16_t stride = over_store ? 1 : value_slots(sequence->element);

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
        if (over_store) {
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
            size_t room = strlen(program->alias) + decl->name.length + 2;
            char *name = kest_arena_alloc(program->arena, room, 1);
            if (name == NULL) {
                return false;
            }
            const char *written = program->source->text + decl->name.offset;
            if (program->alias[0] == '\0') {
                snprintf(name, room, "%.*s", (int)decl->name.length, written);
            } else {
                snprintf(name, room, "%s.%.*s", program->alias,
                         (int)decl->name.length, written);
            }
            KestChunk *chunk = kest_module_add(module, name);
            if (chunk == NULL) {
                return false;
            }
            chunk->source = program->source;
            chunk->returns_value = decl->function.result != NULL;
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

            compiler.chunk = module->functions[index++];
            compiler.unit = u;
            compiler.local_count = 0;
            compiler.next_slot = 0;
            compiler.slot_high_water = 0;
            compiler.stack_depth = 0;
            compiler.stack_high_water = 0;
            compiler.depth = 0;
            compiler.loop_count = 0;

            KestSymbol *symbol = kest_lookup_global(
                program, program->source->text + decl->name.offset,
                decl->name.length);
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

    return !compiler.out_of_memory;
}
