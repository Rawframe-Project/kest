#include "fmt.h"

#include <string.h>

#define MAX_COMMENTS 4096

#define LINE_LIMIT 80
#define MAX_CHAIN 32

typedef struct {
    const KestSource *source;
    KestArena *arena;
    char *buffer;
    size_t used;
    size_t capacity;
    bool out_of_memory;
    int depth;
    // How far along the line the printer is, and whether it is printing at
    // all. Measuring is printing with the writing turned off, so there is one
    // description of what a thing looks like rather than two that can drift.
    uint32_t column;
    bool counting;
    // Set while a condition is being printed. A broken condition indents one
    // level further than a broken anything else, because a condition is the
    // only expression with a block starting one level in right after it.
    bool in_condition;
    // Comments in the order they appear, and how far through them the printer
    // has got. Each is emitted before the first thing that starts after it.
    KestSpan comments[MAX_COMMENTS];
    uint32_t comment_count;
    uint32_t comment_next;
    // Where the last thing printed ended, so a blank line the author left
    // between two things can be left there.
    uint32_t previous_line;
} Printer;

static void put_bytes(Printer *printer, const char *text, size_t length) {
    if (!printer->counting && !printer->out_of_memory) {
        if (printer->used + length + 1 > printer->capacity) {
            size_t capacity = printer->capacity == 0 ? 4096 : printer->capacity;
            while (printer->used + length + 1 > capacity) {
                capacity *= 2;
            }
            char *moved = kest_arena_alloc(printer->arena, capacity, 1);
            if (moved == NULL) {
                printer->out_of_memory = true;
                return;
            }
            if (printer->used > 0) {
                memcpy(moved, printer->buffer, printer->used);
            }
            printer->buffer = moved;
            printer->capacity = capacity;
        }
        memcpy(printer->buffer + printer->used, text, length);
        printer->used += length;
    }
    for (size_t i = 0; i < length; i++) {
        printer->column = text[i] == '\n' ? 0 : printer->column + 1;
    }
}

static void put(Printer *printer, const char *text) {
    put_bytes(printer, text, strlen(text));
}

static void put_char(Printer *printer, char c) {
    put_bytes(printer, &c, 1);
}

static void put_spaces(Printer *printer, int count) {
    for (int i = 0; i < count; i++) {
        put_char(printer, ' ');
    }
}

// A comment runs to the end of its line, and a string may hold two slashes
// that begin nothing, which is the only reason this is not a search.
static void scan_comments(Printer *printer) {
    const char *text = printer->source->text;
    size_t length = printer->source->length;

    for (size_t i = 0; i < length; i++) {
        if (text[i] == '"') {
            // A hole may hold a string of its own, so the quote that closes
            // this one is the one found outside every brace.
            uint32_t depth = 0;
            for (i++; i < length; i++) {
                if (text[i] == '\\') {
                    i++;
                } else if (text[i] == '{') {
                    depth++;
                } else if (text[i] == '}' && depth > 0) {
                    depth--;
                } else if (text[i] == '"' && depth == 0) {
                    break;
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
    put_spaces(printer, printer->depth * 4);
}

static void print_span(Printer *printer, KestSpan span) {
    put_bytes(printer, printer->source->text + span.offset, span.length);
}

// One blank line where the author left one or more, and none where they left
// none. Two blank lines are a preference; one is a paragraph.
static void separate(Printer *printer, uint32_t line) {
    if (printer->previous_line != 0 && line > printer->previous_line + 1) {
        put_char(printer, '\n');
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
        put_char(printer, '\n');
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
        put_char(printer, '<');
        for (uint32_t i = 0; i < type->arg_count; i++) {
            put(printer, i > 0 ? ", " : "");
            print_type(printer, type->args[i]);
        }
        put_char(printer, '>');
        break;
    case KEST_TYPE_ARRAY:
        put_char(printer, '[');
        print_type(printer, type->element);
        if (type->count.length > 0) {
            put(printer, "; ");
            print_span(printer, type->count);
        }
        put_char(printer, ']');
        break;
    case KEST_TYPE_OPTIONAL:
        print_type(printer, type->element);
        put_char(printer, '?');
        break;
    case KEST_TYPE_FN:
        put(printer, "fn(");
        for (uint32_t i = 0; i < type->arg_count; i++) {
            put(printer, i > 0 ? ", " : "");
            print_type(printer, type->args[i]);
        }
        put_char(printer, ')');
        if (type->element != NULL) {
            put(printer, " -> ");
            print_type(printer, type->element);
        }
        if (type->no_alloc) {
            put(printer, " no.alloc");
        }
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
            put_char(printer, *c);
        }
    }
}

static void print_expr(Printer *printer, const KestExpr *expr, int outer);
static void print_block(Printer *printer, const KestBlock *block,
                        uint32_t closing);
static void print_condition(Printer *printer, const KestExpr *expr);
static void lead(Printer *printer, uint32_t offset);

// The condition of a block, which breaks one level deeper than anything else.
static void print_condition(Printer *printer, const KestExpr *expr) {
    bool was = printer->in_condition;
    printer->in_condition = true;
    print_expr(printer, expr, 0);
    printer->in_condition = was;
}

// How wide this would be from here, found by printing it with the writing
// turned off.
static uint32_t measure(Printer *printer, const KestExpr *expr) {
    bool was_counting = printer->counting;
    uint32_t start = printer->column;
    printer->counting = true;
    print_expr(printer, expr, 0);
    uint32_t width = printer->column - start;
    printer->counting = was_counting;
    printer->column = start;
    return width;
}

// A list too long for the line goes one item to a line, all of them or none.
// Half of them on one line and half on the next is the arrangement nobody
// asked for.
static bool fits(Printer *printer, const KestExpr *expr, uint32_t count) {
    // While measuring, the answer is the width of the flat form, which is the
    // thing being measured. Asking again here is how this first went round
    // forever.
    if (printer->counting || count < 2) {
        return true;
    }
    return printer->column + measure(printer, expr) <= LINE_LIMIT;
}

static void print_items(Printer *printer, KestExpr **items, uint32_t count,
                        bool broken) {
    bool was = printer->in_condition;
    printer->in_condition = false;
    if (!broken) {
        for (uint32_t i = 0; i < count; i++) {
            put(printer, i > 0 ? ", " : "");
            print_expr(printer, items[i], 0);
        }
        printer->in_condition = was;
        return;
    }
    printer->depth++;
    for (uint32_t i = 0; i < count; i++) {
        put_char(printer, '\n');
        indent(printer);
        print_expr(printer, items[i], 0);
        if (i + 1 < count) {
            put_char(printer, ',');
        }
    }
    printer->depth--;
    put_char(printer, '\n');
    indent(printer);
    printer->in_condition = was;
}

// A bracket goes back only where taking it away would change what binds to
// what, which is why the tree is what decides and not what was written.
static void print_operand(Printer *printer, const KestExpr *expr, int limit) {
    bool needs =
        expr != NULL && expr->kind == KEST_EXPR_BINARY &&
        precedence_of(expr->binary.op) < limit;
    if (needs) {
        put_char(printer, '(');
    }
    print_expr(printer, expr, needs ? 0 : limit);
    if (needs) {
        put_char(printer, ')');
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
    case KEST_EXPR_BYTE:
    case KEST_EXPR_STRING:
    case KEST_EXPR_TEXT:
        // As written. A number's spelling and a string's contents are the
        // author's, not the formatter's.
        print_span(printer, expr->span);
        break;
    case KEST_EXPR_BOOL:
        put(printer, expr->boolean ? "true" : "false");
        break;
    case KEST_EXPR_NONE:
        put(printer, "none");
        break;
    case KEST_EXPR_UNARY:
        print_operator(printer, expr->unary.op);
        print_operand(printer, expr->unary.operand, 6);
        break;
    case KEST_EXPR_BINARY: {
        int level = precedence_of(expr->binary.op);

        // The tree nests to the left, so `a || b || c` is two nodes and
        // breaking the top one alone would put `(a || b)` on a line by itself.
        // Everything at this precedence is one chain and breaks as one.
        const KestExpr *rights[MAX_CHAIN];
        KestTokenKind operators[MAX_CHAIN];
        uint32_t count = 0;
        const KestExpr *head = expr;
        while (head->kind == KEST_EXPR_BINARY &&
               precedence_of(head->binary.op) == level && count < MAX_CHAIN) {
            rights[count] = head->binary.right;
            operators[count] = head->binary.op;
            count++;
            head = head->binary.left;
        }

        bool broken = !printer->counting && count > 1 &&
                      printer->column + measure(printer, expr) > LINE_LIMIT;

        print_operand(printer, head, level);
        printer->depth += printer->in_condition ? 2 : 1;
        for (uint32_t i = count; i > 0; i--) {
            // The operator ends the line rather than starting the next one,
            // because D003 is what makes the break legal: a line that ends in
            // an operator continues, and one that ends in a value does not.
            put_char(printer, ' ');
            print_operator(printer, operators[i - 1]);
            if (broken) {
                put_char(printer, '\n');
                indent(printer);
            } else {
                put_char(printer, ' ');
            }
            // The right side of a left-associative operator needs a bracket
            // at equal precedence, because without one it would regroup.
            print_operand(printer, rights[i - 1], level + 1);
        }
        printer->depth -= printer->in_condition ? 2 : 1;
        break;
    }
    case KEST_EXPR_CALL: {
        bool broken = !fits(printer, expr, expr->call.arg_count);
        print_expr(printer, expr->call.callee, 6);
        put_char(printer, '(');
        print_items(printer, expr->call.args, expr->call.arg_count, broken);
        put_char(printer, ')');
        break;
    }
    case KEST_EXPR_FIELD:
        print_expr(printer, expr->field.object, 6);
        put_char(printer, '.');
        print_span(printer, expr->field.name);
        break;
    case KEST_EXPR_INDEX:
        print_expr(printer, expr->index.object, 6);
        put_char(printer, '[');
        print_expr(printer, expr->index.index, 0);
        put_char(printer, ']');
        break;
    case KEST_EXPR_IF: {
        const KestBranch *branch = expr->branch;
        put(printer, "if ");
        if (branch->binding.length > 0) {
            put(printer, "let ");
            print_span(printer, branch->binding);
            put(printer, " = ");
        }
        print_condition(printer, branch->condition);
        uint32_t after = expr->span.offset + expr->span.length;
        if (branch->then_value != NULL) {
            put(printer, " -> ");
            print_expr(printer, branch->then_value, 0);
        } else {
            print_block(printer, &branch->then_body, after);
        }
        if (branch->otherwise != NULL) {
            // The chain is one thing to a reader, so its arms carry on the
            // same line rather than each starting one.
            put(printer, " else ");
            print_expr(printer, branch->otherwise, 0);
        } else if (branch->has_else) {
            put(printer, " else");
            if (branch->else_value != NULL) {
                put(printer, " -> ");
                print_expr(printer, branch->else_value, 0);
            } else {
                print_block(printer, &branch->else_body, after);
            }
        }
        break;
    }

    case KEST_EXPR_MATCH: {
        put(printer, "match ");
        for (uint32_t i = 0; i < expr->choose.subject_count; i++) {
            put(printer, i == 0 ? "" : ", ");
            print_condition(printer, expr->choose.subjects[i]);
        }
        put(printer, " {\n");
        printer->depth++;
        uint32_t was = printer->previous_line;
        printer->previous_line = 0;
        for (uint32_t i = 0; i < expr->choose.arm_count; i++) {
            const KestArm *arm = &expr->choose.arms[i];
            lead(printer, arm->span.length > 0 ? arm->span.offset
                                               : expr->span.offset);
            indent(printer);
            for (uint32_t p = 0; p < arm->part_count; p++) {
                const KestArmPart *part = &arm->parts[p];
                put(printer, p == 0 ? "" : ", ");
                if (part->name.length == 0) {
                    put(printer, "else");
                    continue;
                }
                print_span(printer, part->name);
                if (part->binding_count > 0) {
                    put_char(printer, '(');
                    for (uint32_t b = 0; b < part->binding_count; b++) {
                        put(printer, b == 0 ? "" : ", ");
                        print_span(printer, part->bindings[b]);
                    }
                    put_char(printer, ')');
                }
            }
            if (arm->value != NULL) {
                put(printer, " -> ");
                print_expr(printer, arm->value, 0);
                put_char(printer, '\n');
            } else {
                print_block(printer, &arm->body,
                            expr->span.offset + expr->span.length);
                put_char(printer, '\n');
            }
        }
        printer->depth--;
        indent(printer);
        put_char(printer, '}');
        printer->previous_line = was;
        break;
    }

    case KEST_EXPR_ARRAY: {
        bool broken = !fits(printer, expr, expr->array.count);
        put_char(printer, '[');
        print_items(printer, expr->array.items, expr->array.count, broken);
        put_char(printer, ']');
        break;
    }
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
        put(printer, "let ");
        print_span(printer, stmt->let.name);
        if (stmt->let.type != NULL) {
            put(printer, ": ");
            print_type(printer, stmt->let.type);
        }
        put(printer, " = ");
        print_expr(printer, stmt->let.value, 0);
        put_char(printer, '\n');
        break;

    case KEST_STMT_ASSIGN:
        print_expr(printer, stmt->assign.target, 0);
        put_char(printer, ' ');
        print_operator(printer, stmt->assign.op);
        put_char(printer, ' ');
        print_expr(printer, stmt->assign.value, 0);
        put_char(printer, '\n');
        break;

    case KEST_STMT_EXPR:
        print_expr(printer, stmt->value, 0);
        put_char(printer, '\n');
        break;

    case KEST_STMT_DEFER:
        put(printer, "defer ");
        print_expr(printer, stmt->value, 0);
        put_char(printer, '\n');
        break;

    case KEST_STMT_WHILE:
        put(printer, "while ");
        print_condition(printer, stmt->loop.condition);
        print_block(printer, &stmt->loop.body,
                    stmt->span.offset + stmt->span.length);
        put_char(printer, '\n');
        break;

    case KEST_STMT_FOR:
        put(printer, "for ");
        if (stmt->each.index.length > 0) {
            print_span(printer, stmt->each.index);
            put(printer, ", ");
        }
        print_span(printer, stmt->each.name);
        put(printer, " in ");
        print_expr(printer, stmt->each.sequence, 0);
        if (stmt->each.until != NULL) {
            put(printer, "..");
            print_expr(printer, stmt->each.until, 0);
        }
        print_block(printer, &stmt->each.body,
                    stmt->span.offset + stmt->span.length);
        put_char(printer, '\n');
        break;

    case KEST_STMT_RETURN:
        put(printer, "return");
        if (stmt->result != NULL) {
            put_char(printer, ' ');
            print_expr(printer, stmt->result, 0);
        }
        put_char(printer, '\n');
        break;

    case KEST_STMT_BREAK:
        put(printer, "break\n");
        break;

    case KEST_STMT_CONTINUE:
        put(printer, "continue\n");
        break;

    case KEST_STMT_BLOCK:
        put(printer, "{\n");
        printer->depth++;
        printer->previous_line = 0;
        for (uint32_t i = 0; i < stmt->block.count; i++) {
            print_stmt(printer, stmt->block.items[i], false);
        }
        printer->depth--;
        indent(printer);
        put(printer, "}\n");
        break;
    }

    // Where this statement ended, not where it began. A broken argument list
    // makes those different lines, and using the first one put a blank line
    // after every one of them.
    if (stmt->span.length > 0) {
        printer->previous_line =
            line_of(printer, stmt->span.offset + stmt->span.length - 1);
    }
}

static void print_block(Printer *printer, const KestBlock *block,
                        uint32_t closing) {
    put(printer, " {\n");
    printer->depth++;
    // Nothing is separated from the brace that opened it, so a blank line
    // right after `{` goes and a broken condition does not make one.
    printer->previous_line = 0;
    for (uint32_t i = 0; i < block->count; i++) {
        print_stmt(printer, block->items[i], false);
    }
    flush_comments(printer, closing);
    printer->depth--;
    indent(printer);
    put_char(printer, '}');
    // Where the closing brace is, so a blank line the author left after it
    // survives. Forgetting this ate every blank line that followed a block.
    printer->previous_line = closing > 0 ? line_of(printer, closing - 1) : 0;
}

static void print_type_params(Printer *printer, const KestDecl *decl) {
    if (decl->type_param_count == 0) {
        return;
    }
    put_char(printer, '<');
    for (uint32_t i = 0; i < decl->type_param_count; i++) {
        put(printer, i > 0 ? ", " : "");
        print_span(printer, decl->type_params[i]);
    }
    put_char(printer, '>');
}

static void print_signature(Printer *printer, const KestDecl *decl) {
    put(printer, decl->function.is_extern ? "extern fn " : "fn ");
    if (decl->function.receiver.length > 0) {
        print_span(printer, decl->function.receiver);
        put_char(printer, '.');
    }
    print_span(printer, decl->name);
    print_type_params(printer, decl);
    put_char(printer, '(');
    for (uint32_t i = 0; i < decl->function.param_count; i++) {
        put(printer, i > 0 ? ", " : "");
        print_span(printer, decl->function.params[i]->name);
        put(printer, ": ");
        print_type(printer, decl->function.params[i]->type);
    }
    put_char(printer, ')');
    if (decl->function.result != NULL) {
        put(printer, " -> ");
        print_type(printer, decl->function.result);
    }
    if (decl->function.no_alloc) {
        put(printer, " no.alloc");
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
            put_char(printer, '\n');
            printer->previous_line = 0;
        }
    }
    lead(printer, decl->span.offset);

    switch (decl->kind) {
    case KEST_DECL_MODULE:
        put(printer, "module ");
        print_span(printer, decl->name);
        put_char(printer, '\n');
        break;
    case KEST_DECL_IMPORT:
        put(printer, "import ");
        print_span(printer, decl->name);
        put_char(printer, '\n');
        break;
    case KEST_DECL_CONST:
        put(printer, "const ");
        print_span(printer, decl->name);
        put(printer, ": ");
        print_type(printer, decl->constant.type);
        put(printer, " = ");
        print_expr(printer, decl->constant.value, 0);
        put_char(printer, '\n');
        break;
    case KEST_DECL_STRUCT:
        put(printer, "struct ");
        print_span(printer, decl->name);
        print_type_params(printer, decl);
        put(printer, " {\n");
        printer->depth++;
        printer->previous_line = 0;
        for (uint32_t i = 0; i < decl->record.field_count; i++) {
            const KestField *field = decl->record.fields[i];
            lead(printer, field->name.offset);
            indent(printer);
            print_span(printer, field->name);
            put(printer, ": ");
            print_type(printer, field->type);
            put_char(printer, '\n');
        }
        flush_comments(printer, decl->span.offset + decl->span.length);
        printer->depth--;
        put(printer, "}\n");
        break;
    case KEST_DECL_FLAGS:
        put(printer, "flags ");
        print_span(printer, decl->name);
        put(printer, ": ");
        print_type(printer, decl->choice.width);
        put(printer, " {\n");
        printer->depth++;
        printer->previous_line = 0;
        for (uint32_t i = 0; i < decl->choice.case_count; i++) {
            lead(printer, decl->choice.cases[i]->name.offset);
            indent(printer);
            print_span(printer, decl->choice.cases[i]->name);
            put_char(printer, '\n');
        }
        flush_comments(printer, decl->span.offset + decl->span.length);
        printer->depth--;
        put(printer, "}\n");
        break;
    case KEST_DECL_ENUM:
        put(printer, "enum ");
        print_span(printer, decl->name);
        put(printer, " {\n");
        printer->depth++;
        printer->previous_line = 0;
        for (uint32_t i = 0; i < decl->choice.case_count; i++) {
            const KestVariant *variant = decl->choice.cases[i];
            lead(printer, variant->name.offset);
            indent(printer);
            print_span(printer, variant->name);
            if (variant->payload_count > 0) {
                put_char(printer, '(');
                for (uint32_t p = 0; p < variant->payload_count; p++) {
                    put(printer, p == 0 ? "" : ", ");
                    print_type(printer, variant->payload[p]);
                }
                put_char(printer, ')');
            }
            put_char(printer, '\n');
        }
        flush_comments(printer, decl->span.offset + decl->span.length);
        printer->depth--;
        put(printer, "}\n");
        break;
    case KEST_DECL_FN:
        print_signature(printer, decl);
        if (decl->function.is_extern) {
            put_char(printer, '\n');
            break;
        }
        print_block(printer, &decl->function.body,
                    decl->span.offset + decl->span.length);
        put_char(printer, '\n');
        break;
    }
    printer->previous_line = line_of(printer, decl->span.offset +
                                                  decl->span.length);
}

const char *kest_format(const KestUnit *unit, const KestSource *source,
                        KestArena *arena, size_t *length) {
    Printer printer = {0};
    printer.source = source;
    printer.arena = arena;
    scan_comments(&printer);

    for (uint32_t i = 0; i < unit->count; i++) {
        print_decl(&printer, unit->items[i], i == 0 ? NULL : unit->items[i - 1]);
    }
    // Anything written after the last declaration is still the author's.
    flush_comments(&printer, (uint32_t)source->length);

    if (printer.out_of_memory) {
        return NULL;
    }
    if (printer.buffer == NULL) {
        *length = 0;
        return "";
    }
    printer.buffer[printer.used] = '\0';
    *length = printer.used;
    return printer.buffer;
}
