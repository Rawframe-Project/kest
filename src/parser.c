#include "parser.h"

#include <stdarg.h>
#include <string.h>

typedef struct {
    KestArena *arena;
    const KestSource *source;
    KestDiags *diags;
    KestToken *tokens;
    uint32_t count;
    uint32_t position;
    // Set when an error is reported, cleared at a recovery point. One broken
    // construct reports once rather than at every token it goes on to confuse.
    bool recovering;
    bool out_of_memory;
} Parser;

// A pointer list that grows by copying into the arena. Compilation frees the
// arena in one call, so the abandoned copies cost only address space.
typedef struct {
    void **items;
    uint32_t count;
    uint32_t capacity;
} List;

static void list_push(Parser *parser, List *list, void *item) {
    if (list->count == list->capacity) {
        uint32_t capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        void **items = KEST_ARENA_ARRAY(parser->arena, void *, capacity);
        if (items == NULL) {
            parser->out_of_memory = true;
            return;
        }
        memcpy(items, list->items, sizeof(void *) * list->count);
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = item;
}

static KestToken peek(Parser *parser) {
    return parser->tokens[parser->position];
}

static KestToken peek_at(Parser *parser, uint32_t ahead) {
    uint32_t index = parser->position + ahead;
    if (index >= parser->count) {
        index = parser->count - 1;
    }
    return parser->tokens[index];
}

static bool check(Parser *parser, KestTokenKind kind) {
    return peek(parser).kind == kind;
}

static KestToken advance(Parser *parser) {
    KestToken token = peek(parser);
    if (token.kind != KEST_TOK_EOF) {
        parser->position++;
    }
    return token;
}

static bool match(Parser *parser, KestTokenKind kind) {
    if (!check(parser, kind)) {
        return false;
    }
    parser->position++;
    return true;
}

static const char *span_text(Parser *parser, KestSpan span) {
    return parser->source->text + span.offset;
}

// Whether the identifier at `ahead` is spelled `word`.
static bool is_word(Parser *parser, uint32_t ahead, const char *word) {
    KestToken token = peek_at(parser, ahead);
    size_t length = strlen(word);
    return token.kind == KEST_TOK_IDENT && token.span.length == length &&
           memcmp(span_text(parser, token.span), word, length) == 0;
}

static void error_at(Parser *parser, KestSpan span, const char *code,
                     const char *format, ...) {
    if (parser->recovering) {
        return;
    }
    parser->recovering = true;

    va_list args;
    va_start(args, format);
    char message[512];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    kest_diags_add(parser->diags, KEST_SEVERITY_ERROR, code, span, "%s",
                   message);
}

static bool expect(Parser *parser, KestTokenKind kind) {
    if (match(parser, kind)) {
        return true;
    }
    KestToken found = peek(parser);
    error_at(parser, found.span, "K0201", "expected %s, found %s",
             kest_token_name(kind), kest_token_name(found.kind));
    return false;
}

static void skip_newlines(Parser *parser) {
    while (match(parser, KEST_TOK_NEWLINE)) {
    }
}

static bool starts_statement(KestTokenKind kind) {
    switch (kind) {
    case KEST_TOK_LET:
    case KEST_TOK_IF:
    case KEST_TOK_WHILE:
    case KEST_TOK_FOR:
    case KEST_TOK_RETURN:
    case KEST_TOK_BREAK:
    case KEST_TOK_CONTINUE:
        return true;
    default:
        return false;
    }
}

static bool starts_declaration(KestTokenKind kind) {
    switch (kind) {
    case KEST_TOK_MODULE:
    case KEST_TOK_IMPORT:
    case KEST_TOK_CONST:
    case KEST_TOK_STRUCT:
    case KEST_TOK_FN:
    case KEST_TOK_EXTERN:
        return true;
    default:
        return false;
    }
}

// Discards tokens until the next place a statement could begin, so the rest of
// the block is still parsed and still reports its own errors.
static void recover_statement(Parser *parser) {
    while (!check(parser, KEST_TOK_EOF)) {
        if (check(parser, KEST_TOK_NEWLINE)) {
            advance(parser);
            break;
        }
        if (check(parser, KEST_TOK_RBRACE) ||
            starts_statement(peek(parser).kind)) {
            break;
        }
        advance(parser);
    }
    parser->recovering = false;
}

static void recover_declaration(Parser *parser) {
    while (!check(parser, KEST_TOK_EOF) &&
           !starts_declaration(peek(parser).kind)) {
        advance(parser);
    }
    parser->recovering = false;
}

// A statement ends at a line break, or at the brace that closes its block.
static void end_statement(Parser *parser) {
    if (match(parser, KEST_TOK_NEWLINE)) {
        parser->recovering = false;
        return;
    }
    if (check(parser, KEST_TOK_RBRACE) || check(parser, KEST_TOK_EOF)) {
        parser->recovering = false;
        return;
    }
    KestToken found = peek(parser);
    error_at(parser, found.span, "K0201", "expected end of line, found %s",
             kest_token_name(found.kind));
    recover_statement(parser);
}

static KestSpan span_between(KestSpan from, KestSpan to) {
    KestSpan span = {from.offset, to.offset + to.length - from.offset};
    return span;
}

static KestSpan current_span(Parser *parser) {
    return peek(parser).span;
}

// A dotted path such as `game.player`, reported as one name.
static KestSpan parse_path(Parser *parser) {
    KestSpan start = current_span(parser);
    if (!expect(parser, KEST_TOK_IDENT)) {
        return start;
    }
    KestSpan end = parser->tokens[parser->position - 1].span;
    while (check(parser, KEST_TOK_DOT) &&
           peek_at(parser, 1).kind == KEST_TOK_IDENT) {
        advance(parser);
        end = advance(parser).span;
    }
    return span_between(start, end);
}

static KestTypeRef *parse_type(Parser *parser) {
    KestSpan start = current_span(parser);
    KestTypeRef *type = KEST_ARENA_NEW(parser->arena, KestTypeRef);
    if (type == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }

    if (match(parser, KEST_TOK_LBRACKET)) {
        type->kind = KEST_TYPE_ARRAY;
        type->element = parse_type(parser);
        expect(parser, KEST_TOK_RBRACKET);
    } else if (check(parser, KEST_TOK_IDENT)) {
        type->kind = KEST_TYPE_NAMED;
        type->name = advance(parser).span;
        if (match(parser, KEST_TOK_LT)) {
            type->kind = KEST_TYPE_GENERIC;
            List args = {0};
            do {
                list_push(parser, &args, parse_type(parser));
            } while (match(parser, KEST_TOK_COMMA));
            expect(parser, KEST_TOK_GT);
            type->args = (KestTypeRef **)args.items;
            type->arg_count = args.count;
        }
    } else {
        KestToken found = peek(parser);
        error_at(parser, found.span, "K0203", "expected a type, found %s",
                 kest_token_name(found.kind));
        return NULL;
    }

    type->span = span_between(start, parser->tokens[parser->position - 1].span);

    while (check(parser, KEST_TOK_QUESTION)) {
        KestSpan mark = advance(parser).span;
        KestTypeRef *optional = KEST_ARENA_NEW(parser->arena, KestTypeRef);
        if (optional == NULL) {
            parser->out_of_memory = true;
            return type;
        }
        optional->kind = KEST_TYPE_OPTIONAL;
        optional->element = type;
        optional->span = span_between(start, mark);
        type = optional;
    }
    return type;
}

static KestExpr *parse_expr(Parser *parser);

static KestExpr *new_expr(Parser *parser, KestExprKind kind, KestSpan span) {
    KestExpr *expr = KEST_ARENA_NEW(parser->arena, KestExpr);
    if (expr == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }
    expr->kind = kind;
    expr->span = span;
    return expr;
}

static KestExpr *parse_primary(Parser *parser) {
    KestToken token = peek(parser);
    switch (token.kind) {
    case KEST_TOK_INT:
        advance(parser);
        return new_expr(parser, KEST_EXPR_INT, token.span);
    case KEST_TOK_FLOAT:
        advance(parser);
        return new_expr(parser, KEST_EXPR_FLOAT, token.span);
    case KEST_TOK_STRING:
        advance(parser);
        return new_expr(parser, KEST_EXPR_STRING, token.span);
    case KEST_TOK_IDENT:
        advance(parser);
        return new_expr(parser, KEST_EXPR_NAME, token.span);
    case KEST_TOK_TRUE:
    case KEST_TOK_FALSE: {
        advance(parser);
        KestExpr *expr = new_expr(parser, KEST_EXPR_BOOL, token.span);
        if (expr != NULL) {
            expr->boolean = token.kind == KEST_TOK_TRUE;
        }
        return expr;
    }
    case KEST_TOK_LPAREN: {
        advance(parser);
        KestExpr *inner = parse_expr(parser);
        expect(parser, KEST_TOK_RPAREN);
        return inner;
    }
    default:
        error_at(parser, token.span, "K0204", "expected an expression, found %s",
                 kest_token_name(token.kind));
        return NULL;
    }
}

// Calls, field access and indexing, which bind tighter than any operator.
static KestExpr *parse_postfix(Parser *parser) {
    KestExpr *expr = parse_primary(parser);
    if (expr == NULL) {
        return NULL;
    }

    while (true) {
        if (match(parser, KEST_TOK_LPAREN)) {
            List args = {0};
            if (!check(parser, KEST_TOK_RPAREN)) {
                do {
                    KestExpr *arg = parse_expr(parser);
                    if (arg == NULL) {
                        return NULL;
                    }
                    list_push(parser, &args, arg);
                } while (match(parser, KEST_TOK_COMMA));
            }
            KestSpan close = current_span(parser);
            expect(parser, KEST_TOK_RPAREN);

            KestExpr *call = new_expr(parser, KEST_EXPR_CALL,
                                      span_between(expr->span, close));
            if (call == NULL) {
                return NULL;
            }
            call->call.callee = expr;
            call->call.args = (KestExpr **)args.items;
            call->call.arg_count = args.count;
            expr = call;
        } else if (match(parser, KEST_TOK_DOT)) {
            KestSpan name = current_span(parser);
            if (!expect(parser, KEST_TOK_IDENT)) {
                return NULL;
            }
            KestExpr *field = new_expr(parser, KEST_EXPR_FIELD,
                                       span_between(expr->span, name));
            if (field == NULL) {
                return NULL;
            }
            field->field.object = expr;
            field->field.name = name;
            expr = field;
        } else if (match(parser, KEST_TOK_LBRACKET)) {
            KestExpr *subscript = parse_expr(parser);
            KestSpan close = current_span(parser);
            expect(parser, KEST_TOK_RBRACKET);

            KestExpr *index = new_expr(parser, KEST_EXPR_INDEX,
                                       span_between(expr->span, close));
            if (index == NULL) {
                return NULL;
            }
            index->index.object = expr;
            index->index.index = subscript;
            expr = index;
        } else {
            return expr;
        }
    }
}

static KestExpr *parse_unary(Parser *parser) {
    if (check(parser, KEST_TOK_MINUS) || check(parser, KEST_TOK_BANG)) {
        KestToken op = advance(parser);
        KestExpr *operand = parse_unary(parser);
        if (operand == NULL) {
            return NULL;
        }
        KestExpr *expr = new_expr(parser, KEST_EXPR_UNARY,
                                  span_between(op.span, operand->span));
        if (expr == NULL) {
            return NULL;
        }
        expr->unary.op = op.kind;
        expr->unary.operand = operand;
        return expr;
    }
    return parse_postfix(parser);
}

static int binary_precedence(KestTokenKind kind) {
    switch (kind) {
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
    case KEST_TOK_STAR:
    case KEST_TOK_SLASH:
    case KEST_TOK_PERCENT:
        return 6;
    default:
        return 0;
    }
}

static KestExpr *parse_binary(Parser *parser, int minimum) {
    KestExpr *left = parse_unary(parser);
    if (left == NULL) {
        return NULL;
    }

    while (true) {
        int precedence = binary_precedence(peek(parser).kind);
        if (precedence == 0 || precedence < minimum) {
            return left;
        }
        KestToken op = advance(parser);
        // Every operator is left associative, so the right side stops at the
        // first operator of equal precedence.
        KestExpr *right = parse_binary(parser, precedence + 1);
        if (right == NULL) {
            return NULL;
        }
        KestExpr *expr = new_expr(parser, KEST_EXPR_BINARY,
                                  span_between(left->span, right->span));
        if (expr == NULL) {
            return NULL;
        }
        expr->binary.op = op.kind;
        expr->binary.left = left;
        expr->binary.right = right;
        left = expr;
    }
}

static KestExpr *parse_expr(Parser *parser) {
    return parse_binary(parser, 1);
}

static bool parse_block(Parser *parser, KestBlock *block);

static KestStmt *new_stmt(Parser *parser, KestStmtKind kind, KestSpan span) {
    KestStmt *stmt = KEST_ARENA_NEW(parser->arena, KestStmt);
    if (stmt == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }
    stmt->kind = kind;
    stmt->span = span;
    return stmt;
}

static bool is_assignable(const KestExpr *expr) {
    return expr->kind == KEST_EXPR_NAME || expr->kind == KEST_EXPR_FIELD ||
           expr->kind == KEST_EXPR_INDEX;
}

static bool is_assignment(KestTokenKind kind) {
    switch (kind) {
    case KEST_TOK_EQ:
    case KEST_TOK_PLUSEQ:
    case KEST_TOK_MINUSEQ:
    case KEST_TOK_STAREQ:
    case KEST_TOK_SLASHEQ:
        return true;
    default:
        return false;
    }
}

static KestStmt *parse_statement(Parser *parser) {
    KestSpan start = current_span(parser);

    if (match(parser, KEST_TOK_LET)) {
        KestSpan name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT)) {
            return NULL;
        }
        KestTypeRef *type = NULL;
        if (match(parser, KEST_TOK_COLON)) {
            type = parse_type(parser);
        }
        if (!expect(parser, KEST_TOK_EQ)) {
            return NULL;
        }
        KestExpr *value = parse_expr(parser);
        if (value == NULL) {
            return NULL;
        }
        KestStmt *stmt =
            new_stmt(parser, KEST_STMT_LET, span_between(start, value->span));
        if (stmt == NULL) {
            return NULL;
        }
        stmt->let.name = name;
        stmt->let.type = type;
        stmt->let.value = value;
        return stmt;
    }

    if (match(parser, KEST_TOK_IF)) {
        // The condition stops at the opening brace on its own: no expression
        // in the grammar can begin with one. A struct literal would change
        // that and would need a rule here.
        KestExpr *condition = parse_expr(parser);
        if (condition == NULL) {
            return NULL;
        }
        KestStmt *stmt = new_stmt(parser, KEST_STMT_IF, start);
        if (stmt == NULL) {
            return NULL;
        }
        stmt->branch.condition = condition;
        if (!parse_block(parser, &stmt->branch.then_body)) {
            return stmt;
        }
        if (match(parser, KEST_TOK_ELSE)) {
            if (check(parser, KEST_TOK_IF)) {
                stmt->branch.otherwise = parse_statement(parser);
            } else {
                KestStmt *tail =
                    new_stmt(parser, KEST_STMT_BLOCK, current_span(parser));
                if (tail == NULL) {
                    return stmt;
                }
                parse_block(parser, &tail->block);
                stmt->branch.otherwise = tail;
            }
        }
        stmt->span =
            span_between(start, parser->tokens[parser->position - 1].span);
        return stmt;
    }

    if (match(parser, KEST_TOK_WHILE)) {
        KestExpr *condition = parse_expr(parser);
        if (condition == NULL) {
            return NULL;
        }
        KestStmt *stmt = new_stmt(parser, KEST_STMT_WHILE, start);
        if (stmt == NULL) {
            return NULL;
        }
        stmt->loop.condition = condition;
        parse_block(parser, &stmt->loop.body);
        stmt->span =
            span_between(start, parser->tokens[parser->position - 1].span);
        return stmt;
    }

    if (match(parser, KEST_TOK_FOR)) {
        KestSpan name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT)) {
            return NULL;
        }
        if (!expect(parser, KEST_TOK_IN)) {
            return NULL;
        }
        KestExpr *sequence = parse_expr(parser);
        if (sequence == NULL) {
            return NULL;
        }
        KestStmt *stmt = new_stmt(parser, KEST_STMT_FOR, start);
        if (stmt == NULL) {
            return NULL;
        }
        stmt->each.name = name;
        stmt->each.sequence = sequence;
        parse_block(parser, &stmt->each.body);
        stmt->span =
            span_between(start, parser->tokens[parser->position - 1].span);
        return stmt;
    }

    if (match(parser, KEST_TOK_RETURN)) {
        KestStmt *stmt = new_stmt(parser, KEST_STMT_RETURN, start);
        if (stmt == NULL) {
            return NULL;
        }
        if (!check(parser, KEST_TOK_NEWLINE) && !check(parser, KEST_TOK_RBRACE) &&
            !check(parser, KEST_TOK_EOF)) {
            stmt->result = parse_expr(parser);
            if (stmt->result != NULL) {
                stmt->span = span_between(start, stmt->result->span);
            }
        }
        return stmt;
    }

    if (check(parser, KEST_TOK_BREAK) || check(parser, KEST_TOK_CONTINUE)) {
        KestTokenKind kind = advance(parser).kind;
        return new_stmt(parser,
                        kind == KEST_TOK_BREAK ? KEST_STMT_BREAK
                                               : KEST_STMT_CONTINUE,
                        start);
    }

    if (check(parser, KEST_TOK_LBRACE)) {
        KestStmt *stmt = new_stmt(parser, KEST_STMT_BLOCK, start);
        if (stmt == NULL) {
            return NULL;
        }
        parse_block(parser, &stmt->block);
        return stmt;
    }

    KestExpr *expr = parse_expr(parser);
    if (expr == NULL) {
        return NULL;
    }

    if (is_assignment(peek(parser).kind)) {
        KestToken op = advance(parser);
        KestExpr *value = parse_expr(parser);
        if (value == NULL) {
            return NULL;
        }
        if (!is_assignable(expr)) {
            error_at(parser, expr->span, "K0205",
                     "this expression cannot be assigned to");
            kest_diags_suggest(parser->diags,
                               "only a name, a field or an element can be a "
                               "target");
        }
        KestStmt *stmt = new_stmt(parser, KEST_STMT_ASSIGN,
                                  span_between(start, value->span));
        if (stmt == NULL) {
            return NULL;
        }
        stmt->assign.op = op.kind;
        stmt->assign.target = expr;
        stmt->assign.value = value;
        return stmt;
    }

    KestStmt *stmt = new_stmt(parser, KEST_STMT_EXPR, expr->span);
    if (stmt == NULL) {
        return NULL;
    }
    stmt->value = expr;
    return stmt;
}

static bool parse_block(Parser *parser, KestBlock *block) {
    if (!expect(parser, KEST_TOK_LBRACE)) {
        return false;
    }

    List items = {0};
    skip_newlines(parser);
    while (!check(parser, KEST_TOK_RBRACE) && !check(parser, KEST_TOK_EOF)) {
        KestStmt *stmt = parse_statement(parser);
        if (stmt == NULL) {
            recover_statement(parser);
        } else {
            list_push(parser, &items, stmt);
            end_statement(parser);
        }
        skip_newlines(parser);
        if (parser->out_of_memory) {
            return false;
        }
    }
    expect(parser, KEST_TOK_RBRACE);

    block->items = (KestStmt **)items.items;
    block->count = items.count;
    return true;
}

static KestField *parse_field(Parser *parser) {
    KestField *field = KEST_ARENA_NEW(parser->arena, KestField);
    if (field == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }
    field->name = current_span(parser);
    if (!expect(parser, KEST_TOK_IDENT)) {
        return NULL;
    }
    if (!expect(parser, KEST_TOK_COLON)) {
        return NULL;
    }
    field->type = parse_type(parser);
    return field->type == NULL ? NULL : field;
}

// `no.alloc` after a signature. It is spelled with a dot so the namespace can
// hold further contracts without taking more keywords.
static bool match_no_alloc(Parser *parser) {
    if (is_word(parser, 0, "no") && peek_at(parser, 1).kind == KEST_TOK_DOT &&
        is_word(parser, 2, "alloc")) {
        parser->position += 3;
        return true;
    }
    return false;
}

static KestDecl *new_decl(Parser *parser, KestDeclKind kind, KestSpan span) {
    KestDecl *decl = KEST_ARENA_NEW(parser->arena, KestDecl);
    if (decl == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }
    decl->kind = kind;
    decl->span = span;
    return decl;
}

static KestDecl *parse_function(Parser *parser, KestSpan start, bool is_extern) {
    advance(parser);

    KestDecl *decl = new_decl(parser, KEST_DECL_FN, start);
    if (decl == NULL) {
        return NULL;
    }
    decl->function.is_extern = is_extern;

    decl->name = current_span(parser);
    if (!expect(parser, KEST_TOK_IDENT)) {
        return NULL;
    }
    if (match(parser, KEST_TOK_DOT)) {
        decl->function.receiver = decl->name;
        decl->name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT)) {
            return NULL;
        }
        if (!is_extern) {
            error_at(parser, decl->function.receiver, "K0206",
                     "only an extern function names a receiver");
        }
    }

    if (!expect(parser, KEST_TOK_LPAREN)) {
        return NULL;
    }
    List params = {0};
    if (!check(parser, KEST_TOK_RPAREN)) {
        do {
            KestField *param = parse_field(parser);
            if (param == NULL) {
                // A signature that did not parse makes its body meaningless,
                // so recovery goes to the next declaration rather than
                // reporting the body against a signature nobody has.
                return NULL;
            }
            list_push(parser, &params, param);
        } while (match(parser, KEST_TOK_COMMA));
    }
    expect(parser, KEST_TOK_RPAREN);
    decl->function.params = (KestField **)params.items;
    decl->function.param_count = params.count;

    if (match(parser, KEST_TOK_ARROW)) {
        decl->function.result = parse_type(parser);
    }
    decl->function.no_alloc = match_no_alloc(parser);

    if (is_extern) {
        decl->span =
            span_between(start, parser->tokens[parser->position - 1].span);
        return decl;
    }

    if (!parse_block(parser, &decl->function.body)) {
        return NULL;
    }
    decl->span = span_between(start, parser->tokens[parser->position - 1].span);
    return decl;
}

static KestDecl *parse_declaration(Parser *parser) {
    KestSpan start = current_span(parser);

    if (match(parser, KEST_TOK_MODULE)) {
        KestDecl *decl = new_decl(parser, KEST_DECL_MODULE, start);
        if (decl != NULL) {
            decl->name = parse_path(parser);
            decl->span = span_between(start, decl->name);
        }
        return decl;
    }

    if (match(parser, KEST_TOK_IMPORT)) {
        KestDecl *decl = new_decl(parser, KEST_DECL_IMPORT, start);
        if (decl != NULL) {
            decl->name = parse_path(parser);
            decl->span = span_between(start, decl->name);
        }
        return decl;
    }

    if (match(parser, KEST_TOK_CONST)) {
        KestDecl *decl = new_decl(parser, KEST_DECL_CONST, start);
        if (decl == NULL) {
            return NULL;
        }
        decl->name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT)) {
            return NULL;
        }
        // A module-level constant is visible outside the body that defines it,
        // and D005 declares at every boundary rather than inferring across one.
        if (!expect(parser, KEST_TOK_COLON)) {
            return NULL;
        }
        decl->constant.type = parse_type(parser);
        if (!expect(parser, KEST_TOK_EQ)) {
            return NULL;
        }
        decl->constant.value = parse_expr(parser);
        if (decl->constant.value != NULL) {
            decl->span = span_between(start, decl->constant.value->span);
        }
        return decl;
    }

    if (match(parser, KEST_TOK_STRUCT)) {
        KestDecl *decl = new_decl(parser, KEST_DECL_STRUCT, start);
        if (decl == NULL) {
            return NULL;
        }
        decl->name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT)) {
            return NULL;
        }
        if (!expect(parser, KEST_TOK_LBRACE)) {
            return NULL;
        }

        List fields = {0};
        skip_newlines(parser);
        while (!check(parser, KEST_TOK_RBRACE) && !check(parser, KEST_TOK_EOF)) {
            KestField *field = parse_field(parser);
            if (field == NULL) {
                recover_statement(parser);
            } else {
                list_push(parser, &fields, field);
                end_statement(parser);
            }
            skip_newlines(parser);
            if (parser->out_of_memory) {
                return decl;
            }
        }
        KestSpan close = current_span(parser);
        expect(parser, KEST_TOK_RBRACE);

        decl->record.fields = (KestField **)fields.items;
        decl->record.field_count = fields.count;
        decl->span = span_between(start, close);
        return decl;
    }

    if (check(parser, KEST_TOK_EXTERN)) {
        advance(parser);
        if (!check(parser, KEST_TOK_FN)) {
            KestToken found = peek(parser);
            error_at(parser, found.span, "K0201", "expected %s, found %s",
                     kest_token_name(KEST_TOK_FN),
                     kest_token_name(found.kind));
            return NULL;
        }
        return parse_function(parser, start, true);
    }

    if (check(parser, KEST_TOK_FN)) {
        return parse_function(parser, start, false);
    }

    KestToken found = peek(parser);
    error_at(parser, found.span, "K0202",
             "expected a declaration, found %s", kest_token_name(found.kind));
    kest_diags_suggest(parser->diags,
                       "a file holds `module`, `import`, `const`, `struct`, "
                       "`fn` and `extern fn`");
    return NULL;
}

bool kest_parse(KestArena *arena, const KestSource *source, KestDiags *diags,
                KestUnit *unit) {
    Parser parser = {0};
    parser.arena = arena;
    parser.source = source;
    parser.diags = diags;
    parser.tokens = kest_lex_all(arena, source, diags, &parser.count);
    if (parser.tokens == NULL) {
        return false;
    }

    List items = {0};
    skip_newlines(&parser);
    while (!check(&parser, KEST_TOK_EOF)) {
        KestDecl *decl = parse_declaration(&parser);
        if (decl == NULL) {
            recover_declaration(&parser);
        } else {
            list_push(&parser, &items, decl);
            end_statement(&parser);
        }
        skip_newlines(&parser);
        if (parser.out_of_memory) {
            return false;
        }
    }

    unit->items = (KestDecl **)items.items;
    unit->count = items.count;
    return true;
}
