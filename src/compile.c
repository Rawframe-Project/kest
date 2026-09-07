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
    uint32_t depth;
} Local;

typedef struct {
    uint32_t start;
    uint32_t breaks[MAX_BREAKS];
    uint32_t break_count;
} Loop;

typedef struct {
    KestProgram *program;
    KestModule *module;
    KestChunk *chunk;

    Local locals[MAX_LOCALS];
    uint16_t local_count;
    uint16_t slot_high_water;
    uint32_t depth;

    Loop loops[MAX_LOOPS];
    uint32_t loop_count;

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

static void stack_push(Compiler *compiler) {
    compiler->stack_depth++;
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
    stack_push(compiler);
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

static int32_t find_local(Compiler *compiler, KestSpan span) {
    const char *name = span_text(compiler, span);
    for (uint16_t i = compiler->local_count; i > 0; i--) {
        Local *local = &compiler->locals[i - 1];
        if (strlen(local->name) == span.length &&
            memcmp(local->name, name, span.length) == 0) {
            return local->slot;
        }
    }
    return -1;
}

// Locals are a stack, so a slot is the position and a scope is dropped by
// rewinding the count. The high water mark is the frame size.
static uint16_t declare_local(Compiler *compiler, KestSpan span) {
    if (compiler->local_count == MAX_LOCALS) {
        refuse(compiler, span, "K0502", "a function holds at most %d names",
               MAX_LOCALS);
        return 0;
    }
    Local *local = &compiler->locals[compiler->local_count];
    local->name = kest_arena_strndup(compiler->program->arena,
                                     span_text(compiler, span), span.length);
    local->slot = compiler->local_count;
    local->depth = compiler->depth;
    compiler->local_count++;
    if (compiler->local_count > compiler->slot_high_water) {
        compiler->slot_high_water = compiler->local_count;
    }
    return local->slot;
}

static bool is_float(const KestType *type) {
    return type != NULL && type->tag == KEST_T_FLOAT;
}

static bool is_unsigned(const KestType *type) {
    return type != NULL && type->tag == KEST_T_INT && !type->is_signed;
}

// Copies a string literal's content, resolving escapes. The span includes the
// quotes, which is why it starts one in and stops one short.
static const char *literal_text(Compiler *compiler, KestSpan span) {
    const char *raw = span_text(compiler, span) + 1;
    size_t length = span.length >= 2 ? span.length - 2 : 0;

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

static int64_t parse_integer(const char *text, size_t length) {
    int64_t value = 0;
    if (length > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        for (size_t i = 2; i < length; i++) {
            char c = text[i];
            int digit = c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
            value = value * 16 + digit;
        }
        return value;
    }
    for (size_t i = 0; i < length; i++) {
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

static double parse_real(Compiler *compiler, KestSpan span) {
    char buffer[64];
    size_t length = span.length < sizeof(buffer) - 1 ? span.length : 0;
    memcpy(buffer, span_text(compiler, span), length);
    buffer[length] = '\0';
    return strtod(buffer, NULL);
}

static void compile_expr(Compiler *compiler, const KestExpr *expr);

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
    bool unsigned_int = is_unsigned(operand);

    switch (op) {
    case KEST_TOK_PLUS:
        emit(compiler, real ? KEST_OP_ADD_F : KEST_OP_ADD_I, span);
        break;
    case KEST_TOK_MINUS:
        emit(compiler, real ? KEST_OP_SUB_F : KEST_OP_SUB_I, span);
        break;
    case KEST_TOK_STAR:
        emit(compiler, real ? KEST_OP_MUL_F : KEST_OP_MUL_I, span);
        break;
    case KEST_TOK_SLASH:
        emit(compiler,
             real ? KEST_OP_DIV_F
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
    }
}

static void compile_call(Compiler *compiler, const KestExpr *expr) {
    const KestExpr *callee = expr->call.callee;
    if (callee->kind != KEST_EXPR_NAME) {
        refuse(compiler, callee->span, "K0501",
               "only a named function can be called so far");
        return;
    }

    for (uint32_t i = 0; i < expr->call.arg_count; i++) {
        compile_expr(compiler, expr->call.args[i]);
    }

    const char *name = span_text(compiler, callee->span);
    if (callee->span.length == 5 && memcmp(name, "print", 5) == 0) {
        stack_pop(compiler, 1);
        emit(compiler, KEST_OP_PRINT, expr->span);
        return;
    }

    char *owned = kest_arena_strndup(compiler->program->arena, name,
                                     callee->span.length);
    int32_t index = kest_module_find(compiler->module, owned);
    if (index < 0) {
        refuse(compiler, callee->span, "K0501",
               "`%s` has no body to call; extern functions are not linked yet",
               owned);
        return;
    }
    stack_pop(compiler, (uint16_t)expr->call.arg_count);
    if (compiler->module->functions[index]->returns_value) {
        stack_push(compiler);
    }
    emit(compiler, KEST_OP_CALL, expr->span);
    emit(compiler, (uint8_t)index, expr->span);
    emit(compiler, (uint8_t)expr->call.arg_count, expr->span);
}

static void compile_expr(Compiler *compiler, const KestExpr *expr) {
    if (expr == NULL || compiler->out_of_memory) {
        return;
    }

    switch (expr->kind) {
    case KEST_EXPR_INT: {
        KestValue value = {0};
        value.integer =
            parse_integer(span_text(compiler, expr->span), expr->span.length);
        emit_constant(compiler, value, KEST_CONST_INT, expr->span);
        break;
    }
    case KEST_EXPR_FLOAT: {
        KestValue value = {0};
        value.real = parse_real(compiler, expr->span);
        emit_constant(compiler, value, KEST_CONST_FLOAT, expr->span);
        break;
    }
    case KEST_EXPR_STRING: {
        KestValue value = {0};
        value.text = literal_text(compiler, expr->span);
        emit_constant(compiler, value, KEST_CONST_TEXT, expr->span);
        break;
    }
    case KEST_EXPR_BOOL:
        stack_push(compiler);
        emit(compiler, expr->boolean ? KEST_OP_TRUE : KEST_OP_FALSE,
             expr->span);
        break;
    case KEST_EXPR_NAME: {
        int32_t slot = find_local(compiler, expr->span);
        if (slot < 0) {
            refuse(compiler, expr->span, "K0501",
                   "`%.*s` is not a local; constants are not compiled yet",
                   (int)expr->span.length, span_text(compiler, expr->span));
            break;
        }
        stack_push(compiler);
        emit(compiler, KEST_OP_LOAD, expr->span);
        emit_u16(compiler, (uint16_t)slot, expr->span);
        break;
    }
    case KEST_EXPR_UNARY:
        compile_expr(compiler, expr->unary.operand);
        if (expr->unary.op == KEST_TOK_BANG) {
            emit(compiler, KEST_OP_NOT, expr->span);
        } else {
            emit(compiler,
                 is_float(expr->type) ? KEST_OP_NEG_F : KEST_OP_NEG_I,
                 expr->span);
        }
        break;
    case KEST_EXPR_BINARY:
        compile_binary(compiler, expr);
        break;
    case KEST_EXPR_CALL:
        compile_call(compiler, expr);
        break;
    case KEST_EXPR_FIELD:
        refuse(compiler, expr->span, "K0501",
               "struct fields are not compiled yet");
        break;
    case KEST_EXPR_INDEX:
        refuse(compiler, expr->span, "K0501", "arrays are not compiled yet");
        break;
    }
}

static void compile_block(Compiler *compiler, const KestBlock *block);

static void compile_stmt(Compiler *compiler, const KestStmt *stmt) {
    if (compiler->out_of_memory) {
        return;
    }

    switch (stmt->kind) {
    case KEST_STMT_LET: {
        compile_expr(compiler, stmt->let.value);
        uint16_t slot = declare_local(compiler, stmt->let.name);
        stack_pop(compiler, 1);
        emit(compiler, KEST_OP_STORE, stmt->span);
        emit_u16(compiler, slot, stmt->span);
        break;
    }

    case KEST_STMT_ASSIGN: {
        const KestExpr *target = stmt->assign.target;
        if (target->kind != KEST_EXPR_NAME) {
            refuse(compiler, target->span, "K0501",
                   "only a name can be assigned to so far");
            break;
        }
        int32_t slot = find_local(compiler, target->span);
        if (slot < 0) {
            refuse(compiler, target->span, "K0501",
                   "`%.*s` is not a local", (int)target->span.length,
                   span_text(compiler, target->span));
            break;
        }
        if (stmt->assign.op != KEST_TOK_EQ) {
            // A compound assignment is the operator applied to the target and
            // the value, so it loads what it is about to overwrite.
            stack_push(compiler);
            emit(compiler, KEST_OP_LOAD, stmt->span);
            emit_u16(compiler, (uint16_t)slot, stmt->span);
        }
        compile_expr(compiler, stmt->assign.value);
        if (stmt->assign.op != KEST_TOK_EQ) {
            bool real = is_float(target->type);
            switch (stmt->assign.op) {
            case KEST_TOK_PLUSEQ:
                emit(compiler, real ? KEST_OP_ADD_F : KEST_OP_ADD_I,
                     stmt->span);
                break;
            case KEST_TOK_MINUSEQ:
                emit(compiler, real ? KEST_OP_SUB_F : KEST_OP_SUB_I,
                     stmt->span);
                break;
            case KEST_TOK_STAREQ:
                emit(compiler, real ? KEST_OP_MUL_F : KEST_OP_MUL_I,
                     stmt->span);
                break;
            default:
                emit(compiler,
                     real ? KEST_OP_DIV_F
                          : (is_unsigned(target->type) ? KEST_OP_DIV_U
                                                       : KEST_OP_DIV_I),
                     stmt->span);
            }
        }
        stack_pop(compiler, 1);
        emit(compiler, KEST_OP_STORE, stmt->span);
        emit_u16(compiler, (uint16_t)slot, stmt->span);
        break;
    }

    case KEST_STMT_EXPR:
        compile_expr(compiler, stmt->value);
        // A call that returns nothing left nothing behind to discard.
        if (stmt->value != NULL && stmt->value->type != NULL &&
            stmt->value->type->tag != KEST_T_VOID) {
            stack_pop(compiler, 1);
            emit(compiler, KEST_OP_POP, stmt->span);
        }
        break;

    case KEST_STMT_IF: {
        compile_expr(compiler, stmt->branch.condition);
        stack_pop(compiler, 1);
        uint32_t otherwise =
            emit_jump(compiler, KEST_OP_JUMP_FALSE, stmt->span);
        compile_block(compiler, &stmt->branch.then_body);

        if (stmt->branch.otherwise == NULL) {
            patch_jump(compiler, otherwise, stmt->span);
            break;
        }
        uint32_t done = emit_jump(compiler, KEST_OP_JUMP, stmt->span);
        patch_jump(compiler, otherwise, stmt->span);
        compile_stmt(compiler, stmt->branch.otherwise);
        patch_jump(compiler, done, stmt->span);
        break;
    }

    case KEST_STMT_WHILE: {
        if (compiler->loop_count == MAX_LOOPS) {
            refuse(compiler, stmt->span, "K0502", "loops nest too deeply");
            break;
        }
        Loop *loop = &compiler->loops[compiler->loop_count++];
        loop->start = compiler->chunk->code_count;
        loop->break_count = 0;

        compile_expr(compiler, stmt->loop.condition);
        stack_pop(compiler, 1);
        uint32_t exit = emit_jump(compiler, KEST_OP_JUMP_FALSE, stmt->span);
        compile_block(compiler, &stmt->loop.body);
        emit_loop(compiler, loop->start, stmt->span);
        patch_jump(compiler, exit, stmt->span);

        for (uint32_t i = 0; i < loop->break_count; i++) {
            patch_jump(compiler, loop->breaks[i], stmt->span);
        }
        compiler->loop_count--;
        break;
    }

    case KEST_STMT_FOR:
        refuse(compiler, stmt->span, "K0501",
               "`for` walks an array, which is not compiled yet");
        break;

    case KEST_STMT_RETURN:
        if (stmt->result == NULL) {
            emit(compiler, KEST_OP_RETURN_VOID, stmt->span);
        } else {
            compile_expr(compiler, stmt->result);
            stack_pop(compiler, 1);
            emit(compiler, KEST_OP_RETURN, stmt->span);
        }
        break;

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

    case KEST_STMT_CONTINUE:
        if (compiler->loop_count > 0) {
            emit_loop(compiler, compiler->loops[compiler->loop_count - 1].start,
                      stmt->span);
        }
        break;

    case KEST_STMT_BLOCK:
        compile_block(compiler, &stmt->block);
        break;
    }
}

static void compile_block(Compiler *compiler, const KestBlock *block) {
    uint16_t mark = compiler->local_count;
    compiler->depth++;
    for (uint32_t i = 0; i < block->count; i++) {
        compile_stmt(compiler, block->items[i]);
    }
    compiler->depth--;
    compiler->local_count = mark;
}

bool kest_compile(KestProgram *program, const KestUnit *unit,
                  KestModule *module) {
    Compiler compiler = {0};
    compiler.program = program;
    compiler.module = module;

    // Every function is registered before any body is emitted, so a call can
    // name a function declared below it.
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_FN || decl->function.is_extern) {
            continue;
        }
        const char *name =
            kest_arena_strndup(program->arena,
                               program->source->text + decl->name.offset,
                               decl->name.length);
        KestChunk *chunk = kest_module_add(module, name);
        if (chunk == NULL) {
            return false;
        }
        chunk->param_count = (uint8_t)decl->function.param_count;
        chunk->returns_value = decl->function.result != NULL;
    }

    uint32_t index = 0;
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_FN || decl->function.is_extern) {
            continue;
        }

        compiler.chunk = module->functions[index++];
        compiler.local_count = 0;
        compiler.slot_high_water = 0;
        compiler.stack_depth = 0;
        compiler.stack_high_water = 0;
        compiler.depth = 0;
        compiler.loop_count = 0;

        for (uint32_t p = 0; p < decl->function.param_count; p++) {
            declare_local(&compiler, decl->function.params[p]->name);
        }
        compile_block(&compiler, &decl->function.body);
        emit(&compiler, KEST_OP_RETURN_VOID, decl->name);

        compiler.chunk->slot_count = compiler.slot_high_water;
        compiler.chunk->stack_needed = compiler.stack_high_water;
    }

    return !compiler.out_of_memory;
}
