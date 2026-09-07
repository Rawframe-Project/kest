#include "fmt.h"

#include <string.h>

#define MAX_COMMENTS 4096

typedef struct {
    const KestSource *source;
    FILE *out;
    int depth;
    // Comments in the order they appear, and how far through them the printer
    // has got. Each is emitted before the first thing that starts after it.
    KestSpan comments[MAX_COMMENTS];
    uint32_t comment_count;
    uint32_t comment_next;
    // Where the last thing printed ended, so a blank line the author left
    // between two things can be left there.
    uint32_t previous_line;
} Printer;

// A comment runs to the end of its line, and a string may hold two slashes
// that begin nothing, which is the only reason this is not a search.
static void scan_comments(Printer *printer) {
    const char *text = printer->source->text;
    size_t length = printer->source->length;

    for (size_t i = 0; i < length; i++) {
        if (text[i] == '"') {
            for (i++; i < length && text[i] != '"'; i++) {
                if (text[i] == '\\') {
                    i++;
                }
            }
            continue;
        }
        if (text[i] != '/' || i + 1 >= length || text[i + 1] != '/') {
            continue;
        }
        size_t end = i;
        while (end < length && text[end] != '\n') {
            end++;
        }
        if (printer->comment_count < MAX_COMMENTS) {
            KestSpan span = {(uint32_t)i, (uint32_t)(end - i)};
            printer->comments[printer->comment_count++] = span;
        }
        i = end;
    }
}

static uint32_t line_of(Printer *printer, uint32_t offset) {
    uint32_t line = 0;
    uint32_t column = 0;
    kest_source_locate(printer->source, offset, &line, &column);
    return line;
}

static void indent(Printer *printer) {
    fprintf(printer->out, "%*s", printer->depth * 4, "");
}

static void print_span(Printer *printer, KestSpan span) {
    fprintf(printer->out, "%.*s", (int)span.length,
            printer->source->text + span.offset);
}

// One blank line where the author left one or more, and none where they left
// none. Two blank lines are a preference; one is a paragraph.
static void separate(Printer *printer, uint32_t line) {
    if (printer->previous_line != 0 && line > printer->previous_line + 1) {
        fputc('\n', printer->out);
    }
}

// Everything written before `offset` comes out first, at the indent of what it
// was written above.
static void flush_comments(Printer *printer, uint32_t offset) {
    while (printer->comment_next < printer->comment_count &&
           printer->comments[printer->comment_next].offset < offset) {
        KestSpan span = printer->comments[printer->comment_next++];
        separate(printer, line_of(printer, span.offset));
        indent(printer);
        print_span(printer, span);
        fputc('\n', printer->out);
        printer->previous_line = line_of(printer, span.offset);
    }
}

// What comes before a thing: its comments, then a blank line if there was one.
static void lead(Printer *printer, uint32_t offset) {
    flush_comments(printer, offset);
    separate(printer, line_of(printer, offset));
    printer->previous_line = line_of(printer, offset);
}

static void print_type(Printer *printer, const KestTypeRef *type) {
    if (type == NULL) {
        return;
    }
    switch (type->kind) {
    case KEST_TYPE_NAMED:
        print_span(printer, type->name);
        break;
    case KEST_TYPE_GENERIC:
        print_span(printer, type->name);
        fputc('<', printer->out);
        for (uint32_t i = 0; i < type->arg_count; i++) {
            fputs(i > 0 ? ", " : "", printer->out);
            print_type(printer, type->args[i]);
        }
        fputc('>', printer->out);
        break;
    case KEST_TYPE_ARRAY:
        fputc('[', printer->out);
        print_type(printer, type->element);
        fputc(']', printer->out);
        break;
    case KEST_TYPE_OPTIONAL:
        print_type(printer, type->element);
        fputc('?', printer->out);
        break;
    }
}

static int precedence_of(KestTokenKind op) {
    switch (op) {
    case KEST_TOK_PIPEPIPE:
        return 1;
    case KEST_TOK_AMPAMP:
        return 2;
    case KEST_TOK_EQEQ:
    case KEST_TOK_BANGEQ:
        return 3;
    case KEST_TOK_LT:
    case KEST_TOK_LTEQ:
    case KEST_TOK_GT:
    case KEST_TOK_GTEQ:
        return 4;
    case KEST_TOK_PLUS:
    case KEST_TOK_MINUS:
        return 5;
    default:
        return 6;
    }
}

static void print_operator(Printer *printer, KestTokenKind op) {
    const char *name = kest_token_name(op);
    for (const char *c = name; *c != '\0'; c++) {
        if (*c != '`') {
            fputc(*c, printer->out);
        }
    }
}

static void print_expr(Printer *printer, const KestExpr *expr, int outer);

// A bracket goes back only where taking it away would change what binds to
// what, which is why the tree is what decides and not what was written.
static void print_operand(Printer *printer, const KestExpr *expr, int limit) {
    bool needs =
        expr != NULL && expr->kind == KEST_EXPR_BINARY &&
        precedence_of(expr->binary.op) < limit;
    if (needs) {
        fputc('(', printer->out);
    }
    print_expr(printer, expr, needs ? 0 : limit);
    if (needs) {
        fputc(')', printer->out);
    }
}

static void print_expr(Printer *printer, const KestExpr *expr, int outer) {
    (void)outer;
    if (expr == NULL) {
        return;
    }

    switch (expr->kind) {
    case KEST_EXPR_INT:
    case KEST_EXPR_FLOAT:
    case KEST_EXPR_NAME:
    case KEST_EXPR_STRING:
    case KEST_EXPR_TEXT:
        // As written. A number's spelling and a string's contents are the
        // author's, not the formatter's.
        print_span(printer, expr->span);
        break;
    case KEST_EXPR_BOOL:
        fputs(expr->boolean ? "true" : "false", printer->out);
        break;
    case KEST_EXPR_NONE:
        fputs("none", printer->out);
        break;
    case KEST_EXPR_UNARY:
        print_operator(printer, expr->unary.op);
        print_operand(printer, expr->unary.operand, 6);
        break;
    case KEST_EXPR_BINARY: {
        int level = precedence_of(expr->binary.op);
        print_operand(printer, expr->binary.left, level);
        fputc(' ', printer->out);
        print_operator(printer, expr->binary.op);
        fputc(' ', printer->out);
        // The right side of a left-associative operator needs a bracket at
        // equal precedence, because without one it would regroup.
        print_operand(printer, expr->binary.right, level + 1);
        break;
    }
    case KEST_EXPR_CALL:
        print_expr(printer, expr->call.callee, 6);
        fputc('(', printer->out);
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            fputs(i > 0 ? ", " : "", printer->out);
            print_expr(printer, expr->call.args[i], 0);
        }
        fputc(')', printer->out);
        break;
    case KEST_EXPR_FIELD:
        print_expr(printer, expr->field.object, 6);
        fputc('.', printer->out);
        print_span(printer, expr->field.name);
        break;
    case KEST_EXPR_INDEX:
        print_expr(printer, expr->index.object, 6);
        fputc('[', printer->out);
        print_expr(printer, expr->index.index, 0);
        fputc(']', printer->out);
        break;
    case KEST_EXPR_ARRAY:
        fputc('[', printer->out);
        for (uint32_t i = 0; i < expr->array.count; i++) {
            fputs(i > 0 ? ", " : "", printer->out);
            print_expr(printer, expr->array.items[i], 0);
        }
        fputc(']', printer->out);
        break;
    }
}

static void print_block(Printer *printer, const KestBlock *block,
                        uint32_t closing);

// `bare` is set for the arm of an `else if`, which continues a line rather
// than starting one, and therefore takes neither the comments above it nor the
// indent.
static void print_stmt(Printer *printer, const KestStmt *stmt, bool bare) {
    if (!bare) {
        lead(printer, stmt->span.offset);
        indent(printer);
    }

    switch (stmt->kind) {
    case KEST_STMT_LET:
        fputs("let ", printer->out);
        print_span(printer, stmt->let.name);
        if (stmt->let.type != NULL) {
            fputs(": ", printer->out);
            print_type(printer, stmt->let.type);
        }
        fputs(" = ", printer->out);
        print_expr(printer, stmt->let.value, 0);
        fputc('\n', printer->out);
        break;

    case KEST_STMT_ASSIGN:
        print_expr(printer, stmt->assign.target, 0);
        fputc(' ', printer->out);
        print_operator(printer, stmt->assign.op);
        fputc(' ', printer->out);
        print_expr(printer, stmt->assign.value, 0);
        fputc('\n', printer->out);
        break;

    case KEST_STMT_EXPR:
        print_expr(printer, stmt->value, 0);
        fputc('\n', printer->out);
        break;

    case KEST_STMT_IF:
        fputs("if ", printer->out);
        if (stmt->branch.binding.length > 0) {
            fputs("let ", printer->out);
            print_span(printer, stmt->branch.binding);
            fputs(" = ", printer->out);
        }
        print_expr(printer, stmt->branch.condition, 0);
        print_block(printer, &stmt->branch.then_body,
                    stmt->span.offset + stmt->span.length);
        if (stmt->branch.otherwise != NULL) {
            const KestStmt *tail = stmt->branch.otherwise;
            fputs(" else", printer->out);
            if (tail->kind == KEST_STMT_IF) {
                // The chain is one statement to a reader, so its arms carry on
                // the same line rather than each starting one.
                fputc(' ', printer->out);
                print_stmt(printer, tail, true);
                return;
            }
            print_block(printer, &tail->block,
                        stmt->span.offset + stmt->span.length);
        }
        fputc('\n', printer->out);
        break;

    case KEST_STMT_WHILE:
        fputs("while ", printer->out);
        print_expr(printer, stmt->loop.condition, 0);
        print_block(printer, &stmt->loop.body,
                    stmt->span.offset + stmt->span.length);
        fputc('\n', printer->out);
        break;

    case KEST_STMT_FOR:
        fputs("for ", printer->out);
        if (stmt->each.index.length > 0) {
            print_span(printer, stmt->each.index);
            fputs(", ", printer->out);
        }
        print_span(printer, stmt->each.name);
        fputs(" in ", printer->out);
        print_expr(printer, stmt->each.sequence, 0);
        print_block(printer, &stmt->each.body,
                    stmt->span.offset + stmt->span.length);
        fputc('\n', printer->out);
        break;

    case KEST_STMT_RETURN:
        fputs("return", printer->out);
        if (stmt->result != NULL) {
            fputc(' ', printer->out);
            print_expr(printer, stmt->result, 0);
        }
        fputc('\n', printer->out);
        break;

    case KEST_STMT_BREAK:
        fputs("break\n", printer->out);
        break;

    case KEST_STMT_CONTINUE:
        fputs("continue\n", printer->out);
        break;

    case KEST_STMT_BLOCK:
        fputs("{\n", printer->out);
        printer->depth++;
        for (uint32_t i = 0; i < stmt->block.count; i++) {
            print_stmt(printer, stmt->block.items[i], false);
        }
        printer->depth--;
        indent(printer);
        fputs("}\n", printer->out);
        break;
    }
}

static void print_block(Printer *printer, const KestBlock *block,
                        uint32_t closing) {
    fputs(" {\n", printer->out);
    printer->depth++;
    for (uint32_t i = 0; i < block->count; i++) {
        print_stmt(printer, block->items[i], false);
    }
    flush_comments(printer, closing);
    printer->depth--;
    indent(printer);
    fputc('}', printer->out);
    // Where the closing brace is, so a blank line the author left after it
    // survives. Forgetting this ate every blank line that followed a block.
    printer->previous_line = closing > 0 ? line_of(printer, closing - 1) : 0;
}

static void print_signature(Printer *printer, const KestDecl *decl) {
    fputs(decl->function.is_extern ? "extern fn " : "fn ", printer->out);
    if (decl->function.receiver.length > 0) {
        print_span(printer, decl->function.receiver);
        fputc('.', printer->out);
    }
    print_span(printer, decl->name);
    fputc('(', printer->out);
    for (uint32_t i = 0; i < decl->function.param_count; i++) {
        fputs(i > 0 ? ", " : "", printer->out);
        print_span(printer, decl->function.params[i]->name);
        fputs(": ", printer->out);
        print_type(printer, decl->function.params[i]->type);
    }
    fputc(')', printer->out);
    if (decl->function.result != NULL) {
        fputs(" -> ", printer->out);
        print_type(printer, decl->function.result);
    }
    if (decl->function.no_alloc) {
        fputs(" no.alloc", printer->out);
    }
}

// A declaration that occupies one line. A run of them is a list and reads as
// one; anything with a body is a paragraph.
static bool is_one_liner(const KestDecl *decl) {
    return decl->kind == KEST_DECL_MODULE || decl->kind == KEST_DECL_IMPORT ||
           decl->kind == KEST_DECL_CONST ||
           (decl->kind == KEST_DECL_FN && decl->function.is_extern);
}

static void print_decl(Printer *printer, const KestDecl *decl,
                       const KestDecl *previous) {
    // Declarations are paragraphs. Two of them run together only when both
    // are one line and the author had them that way.
    if (previous != NULL) {
        bool tight = is_one_liner(previous) && is_one_liner(decl) &&
                     line_of(printer, decl->span.offset) <=
                         printer->previous_line + 1;
        if (!tight) {
            // The blank goes above whatever was written about the
            // declaration, not between it and the declaration.
            fputc('\n', printer->out);
            printer->previous_line = 0;
        }
    }
    lead(printer, decl->span.offset);

    switch (decl->kind) {
    case KEST_DECL_MODULE:
        fputs("module ", printer->out);
        print_span(printer, decl->name);
        fputc('\n', printer->out);
        break;
    case KEST_DECL_IMPORT:
        fputs("import ", printer->out);
        print_span(printer, decl->name);
        fputc('\n', printer->out);
        break;
    case KEST_DECL_CONST:
        fputs("const ", printer->out);
        print_span(printer, decl->name);
        fputs(": ", printer->out);
        print_type(printer, decl->constant.type);
        fputs(" = ", printer->out);
        print_expr(printer, decl->constant.value, 0);
        fputc('\n', printer->out);
        break;
    case KEST_DECL_STRUCT:
        fputs("struct ", printer->out);
        print_span(printer, decl->name);
        fputs(" {\n", printer->out);
        printer->depth++;
        for (uint32_t i = 0; i < decl->record.field_count; i++) {
            const KestField *field = decl->record.fields[i];
            lead(printer, field->name.offset);
            indent(printer);
            print_span(printer, field->name);
            fputs(": ", printer->out);
            print_type(printer, field->type);
            fputc('\n', printer->out);
        }
        flush_comments(printer, decl->span.offset + decl->span.length);
        printer->depth--;
        fputs("}\n", printer->out);
        break;
    case KEST_DECL_FN:
        print_signature(printer, decl);
        if (decl->function.is_extern) {
            fputc('\n', printer->out);
            break;
        }
        print_block(printer, &decl->function.body,
                    decl->span.offset + decl->span.length);
        fputc('\n', printer->out);
        break;
    }
    printer->previous_line = line_of(printer, decl->span.offset +
                                                  decl->span.length);
}

void kest_format(const KestUnit *unit, const KestSource *source, FILE *out) {
    Printer printer = {0};
    printer.source = source;
    printer.out = out;
    scan_comments(&printer);

    for (uint32_t i = 0; i < unit->count; i++) {
        print_decl(&printer, unit->items[i], i == 0 ? NULL : unit->items[i - 1]);
    }
    // Anything written after the last declaration is still the author's.
    flush_comments(&printer, (uint32_t)source->length);
}
