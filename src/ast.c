#include "ast.h"

static void indent(FILE *out, int depth) {
    fprintf(out, "%*s", depth * 2, "");
}

static void print_span(const KestSource *source, KestSpan span, FILE *out) {
    fprintf(out, "%.*s", (int)span.length, source->text + span.offset);
}

// The token name without the backticks it carries for diagnostics.
static void print_op(KestTokenKind kind, FILE *out) {
    const char *name = kest_token_name(kind);
    for (const char *c = name; *c != '\0'; c++) {
        if (*c != '`') {
            fputc(*c, out);
        }
    }
}

static void print_type(const KestTypeRef *type, const KestSource *source,
                       FILE *out) {
    if (type == NULL) {
        fputs("?", out);
        return;
    }
    switch (type->kind) {
    case KEST_TYPE_NAMED:
        print_span(source, type->name, out);
        break;
    case KEST_TYPE_GENERIC:
        print_span(source, type->name, out);
        fputc('<', out);
        for (uint32_t i = 0; i < type->arg_count; i++) {
            if (i > 0) {
                fputs(", ", out);
            }
            print_type(type->args[i], source, out);
        }
        fputc('>', out);
        break;
    case KEST_TYPE_ARRAY:
        fputc('[', out);
        print_type(type->element, source, out);
        fputc(']', out);
        break;
    case KEST_TYPE_OPTIONAL:
        print_type(type->element, source, out);
        fputc('?', out);
        break;
    }
}

static void print_expr(const KestExpr *expr, const KestSource *source,
                       FILE *out) {
    if (expr == NULL) {
        fputs("<error>", out);
        return;
    }
    switch (expr->kind) {
    case KEST_EXPR_INT:
    case KEST_EXPR_FLOAT:
    case KEST_EXPR_NAME:
    case KEST_EXPR_STRING:
        print_span(source, expr->span, out);
        break;
    case KEST_EXPR_BOOL:
        fputs(expr->boolean ? "true" : "false", out);
        break;
    case KEST_EXPR_NONE:
        fputs("none", out);
        break;
    case KEST_EXPR_TEXT:
        fputs("(text", out);
        for (uint32_t i = 0; i < expr->text.count; i++) {
            fputc(' ', out);
            if (expr->text.parts[i].value != NULL) {
                print_expr(expr->text.parts[i].value, source, out);
            } else {
                fputc('"', out);
                print_span(source, expr->text.parts[i].text, out);
                fputc('"', out);
            }
        }
        fputc(')', out);
        break;
    case KEST_EXPR_UNARY:
        fputc('(', out);
        print_op(expr->unary.op, out);
        fputc(' ', out);
        print_expr(expr->unary.operand, source, out);
        fputc(')', out);
        break;
    case KEST_EXPR_BINARY:
        fputc('(', out);
        print_op(expr->binary.op, out);
        fputc(' ', out);
        print_expr(expr->binary.left, source, out);
        fputc(' ', out);
        print_expr(expr->binary.right, source, out);
        fputc(')', out);
        break;
    case KEST_EXPR_CALL:
        fputs("(call ", out);
        print_expr(expr->call.callee, source, out);
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            fputc(' ', out);
            print_expr(expr->call.args[i], source, out);
        }
        fputc(')', out);
        break;
    case KEST_EXPR_FIELD:
        fputs("(. ", out);
        print_expr(expr->field.object, source, out);
        fputc(' ', out);
        print_span(source, expr->field.name, out);
        fputc(')', out);
        break;
    case KEST_EXPR_ARRAY:
        fputs("(array", out);
        for (uint32_t i = 0; i < expr->array.count; i++) {
            fputc(' ', out);
            print_expr(expr->array.items[i], source, out);
        }
        fputc(')', out);
        break;
    case KEST_EXPR_INDEX:
        fputs("(index ", out);
        print_expr(expr->index.object, source, out);
        fputc(' ', out);
        print_expr(expr->index.index, source, out);
        fputc(')', out);
        break;
    }
}

static void print_block(const KestBlock *block, const KestSource *source,
                        int depth, FILE *out);

static void print_stmt(const KestStmt *stmt, const KestSource *source,
                       int depth, FILE *out) {
    indent(out, depth);
    switch (stmt->kind) {
    case KEST_STMT_LET:
        fputs("(let ", out);
        print_span(source, stmt->let.name, out);
        fputs(" : ", out);
        print_type(stmt->let.type, source, out);
        fputc(' ', out);
        print_expr(stmt->let.value, source, out);
        fputs(")\n", out);
        break;
    case KEST_STMT_ASSIGN:
        fputc('(', out);
        print_op(stmt->assign.op, out);
        fputc(' ', out);
        print_expr(stmt->assign.target, source, out);
        fputc(' ', out);
        print_expr(stmt->assign.value, source, out);
        fputs(")\n", out);
        break;
    case KEST_STMT_EXPR:
        print_expr(stmt->value, source, out);
        fputc('\n', out);
        break;
    case KEST_STMT_IF:
        fputs("(if ", out);
        if (stmt->branch.binding.length > 0) {
            fputs("let ", out);
            print_span(source, stmt->branch.binding, out);
            fputc(' ', out);
        }
        print_expr(stmt->branch.condition, source, out);
        fputc('\n', out);
        print_block(&stmt->branch.then_body, source, depth + 1, out);
        if (stmt->branch.otherwise != NULL) {
            indent(out, depth + 1);
            fputs("else\n", out);
            print_stmt(stmt->branch.otherwise, source, depth + 1, out);
        }
        indent(out, depth);
        fputs(")\n", out);
        break;
    case KEST_STMT_WHILE:
        fputs("(while ", out);
        print_expr(stmt->loop.condition, source, out);
        fputc('\n', out);
        print_block(&stmt->loop.body, source, depth + 1, out);
        indent(out, depth);
        fputs(")\n", out);
        break;
    case KEST_STMT_FOR:
        fputs("(for ", out);
        if (stmt->each.index.length > 0) {
            print_span(source, stmt->each.index, out);
            fputs(", ", out);
        }
        print_span(source, stmt->each.name, out);
        fputs(" in ", out);
        print_expr(stmt->each.sequence, source, out);
        fputc('\n', out);
        print_block(&stmt->each.body, source, depth + 1, out);
        indent(out, depth);
        fputs(")\n", out);
        break;
    case KEST_STMT_RETURN:
        fputs("(return", out);
        if (stmt->result != NULL) {
            fputc(' ', out);
            print_expr(stmt->result, source, out);
        }
        fputs(")\n", out);
        break;
    case KEST_STMT_BREAK:
        fputs("(break)\n", out);
        break;
    case KEST_STMT_CONTINUE:
        fputs("(continue)\n", out);
        break;
    case KEST_STMT_MATCH:
        fputs("(match ", out);
        print_expr(stmt->choose.subject, source, out);
        fputc('\n', out);
        for (uint32_t i = 0; i < stmt->choose.arm_count; i++) {
            const KestArm *arm = &stmt->choose.arms[i];
            indent(out, depth + 1);
            fputc('(', out);
            if (arm->name.length == 0) {
                fputs("else", out);
            } else {
                print_span(source, arm->name, out);
            }
            for (uint32_t b = 0; b < arm->binding_count; b++) {
                fputc(' ', out);
                print_span(source, arm->bindings[b], out);
            }
            fputc('\n', out);
            print_block(&arm->body, source, depth + 2, out);
            indent(out, depth + 1);
            fputs(")\n", out);
        }
        indent(out, depth);
        fputs(")\n", out);
        break;
    case KEST_STMT_BLOCK:
        fputs("(block\n", out);
        print_block(&stmt->block, source, depth + 1, out);
        indent(out, depth);
        fputs(")\n", out);
        break;
    }
}

static void print_block(const KestBlock *block, const KestSource *source,
                        int depth, FILE *out) {
    for (uint32_t i = 0; i < block->count; i++) {
        print_stmt(block->items[i], source, depth, out);
    }
}

static void print_decl(const KestDecl *decl, const KestSource *source,
                       FILE *out) {
    switch (decl->kind) {
    case KEST_DECL_MODULE:
        fputs("(module ", out);
        print_span(source, decl->name, out);
        fputs(")\n", out);
        break;
    case KEST_DECL_IMPORT:
        fputs("(import ", out);
        print_span(source, decl->name, out);
        fputs(")\n", out);
        break;
    case KEST_DECL_CONST:
        fputs("(const ", out);
        print_span(source, decl->name, out);
        fputs(" : ", out);
        print_type(decl->constant.type, source, out);
        fputc(' ', out);
        print_expr(decl->constant.value, source, out);
        fputs(")\n", out);
        break;
    case KEST_DECL_STRUCT:
        fputs("(struct ", out);
        print_span(source, decl->name, out);
        fputc('\n', out);
        for (uint32_t i = 0; i < decl->record.field_count; i++) {
            indent(out, 1);
            fputs("(field ", out);
            print_span(source, decl->record.fields[i]->name, out);
            fputc(' ', out);
            print_type(decl->record.fields[i]->type, source, out);
            fputs(")\n", out);
        }
        fputs(")\n", out);
        break;
    case KEST_DECL_ENUM:
        fputs("(enum ", out);
        print_span(source, decl->name, out);
        fputc('\n', out);
        for (uint32_t i = 0; i < decl->choice.case_count; i++) {
            indent(out, 1);
            fputs("(case ", out);
            print_span(source, decl->choice.cases[i]->name, out);
            for (uint32_t p = 0; p < decl->choice.cases[i]->payload_count; p++) {
                fputc(' ', out);
                print_type(decl->choice.cases[i]->payload[p], source, out);
            }
            fputs(")\n", out);
        }
        fputs(")\n", out);
        break;
    case KEST_DECL_FN:
        fputs(decl->function.is_extern ? "(extern fn " : "(fn ", out);
        if (decl->function.receiver.length > 0) {
            print_span(source, decl->function.receiver, out);
            fputc('.', out);
        }
        print_span(source, decl->name, out);
        if (decl->function.no_alloc) {
            fputs(" no.alloc", out);
        }
        fputc('\n', out);
        for (uint32_t i = 0; i < decl->function.param_count; i++) {
            indent(out, 1);
            fputs("(param ", out);
            print_span(source, decl->function.params[i]->name, out);
            fputc(' ', out);
            print_type(decl->function.params[i]->type, source, out);
            fputs(")\n", out);
        }
        if (decl->function.result != NULL) {
            indent(out, 1);
            fputs("(result ", out);
            print_type(decl->function.result, source, out);
            fputs(")\n", out);
        }
        if (!decl->function.is_extern) {
            print_block(&decl->function.body, source, 1, out);
        }
        fputs(")\n", out);
        break;
    }
}

void kest_ast_dump(const KestUnit *unit, const KestSource *source, FILE *out) {
    for (uint32_t i = 0; i < unit->count; i++) {
        print_decl(unit->items[i], source, out);
    }
}
