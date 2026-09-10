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
    // What the statement being parsed began with, so a message about where it
    // ended can point back at the word that started it.
    KestToken began_with;
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
        if (list->count > 0) {
            memcpy(items, list->items, sizeof(void *) * list->count);
        }
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
    // A token the lexer could not read has already been reported, by the one
    // that knows what is wrong with it. Saying something else about the same
    // place is saying it twice, and the second thing is always vaguer.
    KestToken here = peek(parser);
    if (here.kind == KEST_TOK_ERROR && here.span.offset == span.offset) {
        parser->recovering = true;
        return;
    }
    parser->recovering = true;

    // The end of a file is a place, and a span with nothing in it is shown as
    // no place at all: a path with no line under it, on the one message a
    // reader most needs pointed at. So it points just past the last character
    // there is, which is where the file ran out.
    if (span.length == 0 && parser->source->length > 0) {
        uint32_t last = (uint32_t)parser->source->length;
        while (last > 0 && (parser->source->text[last - 1] == '\n' ||
                            parser->source->text[last - 1] == '\r')) {
            last--;
        }
        span.offset = last;
        span.length = 1;
    }

    va_list args;
    va_start(args, format);
    kest_diags_addv(parser->diags, KEST_SEVERITY_ERROR, code, span, format,
                    args);
    va_end(args);
}

// Whether what comes next is a condition written inside brackets and nothing
// else. `if (x < 3) { }` means what `if x < 3 { }` means, and the formatter
// takes the brackets away — so a file written the first way is a second
// spelling of one thing, and this parser accepts one. What says the brackets
// are the whole of it is what follows the one that closes them: a brace, or
// the arrow of an `if` that gives a value. Anything else and they are a
// grouping inside a bigger condition, which is a reader's to write.
static bool wrapped_whole(Parser *parser) {
    if (!check(parser, KEST_TOK_LPAREN)) {
        return false;
    }
    uint32_t deep = 0;
    for (uint32_t at = 0; at < parser->count; at++) {
        KestTokenKind kind = peek_at(parser, at).kind;
        if (kind == KEST_TOK_LPAREN) {
            deep++;
        } else if (kind == KEST_TOK_RPAREN) {
            deep--;
            if (deep == 0) {
                KestTokenKind after = peek_at(parser, at + 1).kind;
                return after == KEST_TOK_LBRACE || after == KEST_TOK_ARROW;
            }
        } else if (kind == KEST_TOK_EOF) {
            return false;
        }
    }
    return false;
}

// `%=` and its like. Four of them are written — `+=`, `-=`, `*=` and `/=` —
// and the rest are not, because a fifth that appears once in a file is written
// out. What a reader who writes one gets otherwise is the parser meeting an `=`
// where a value belongs, which says where it stopped and not what is wrong.
static const char *no_compound(Parser *parser, uint32_t *at_out) {
    uint32_t deep = 0;
    for (uint32_t at = 0; at < parser->count; at++) {
        KestTokenKind kind = peek_at(parser, at).kind;
        if (kind == KEST_TOK_LPAREN || kind == KEST_TOK_LBRACKET) {
            deep++;
            continue;
        }
        if (kind == KEST_TOK_RPAREN || kind == KEST_TOK_RBRACKET) {
            deep--;
            continue;
        }
        // One statement's worth. A brace begins a block and a line end ends a
        // statement, and neither is somewhere this could still be about.
        if (kind == KEST_TOK_NEWLINE || kind == KEST_TOK_EOF ||
            kind == KEST_TOK_LBRACE) {
            return NULL;
        }
        if (deep != 0 || peek_at(parser, at + 1).kind != KEST_TOK_EQ) {
            continue;
        }
        *at_out = at;
        switch (kind) {
        case KEST_TOK_PERCENT:
            return "%";
        case KEST_TOK_AMP:
            return "&";
        case KEST_TOK_PIPE:
            return "|";
        case KEST_TOK_CARET:
            return "^";
        case KEST_TOK_LTLT:
            return "<<";
        case KEST_TOK_GTGT:
            return ">>";
        default:
            break;
        }
    }
    return NULL;
}

// Said where the brackets are, because that is what to take away.
static void refuse_wrapped(Parser *parser, const char *what) {
    error_at(parser, peek(parser).span, "K0213",
             "a condition is written without brackets round the whole of it");
    kest_diags_suggest(parser->diags, "write `%s x < 3 {`", what);
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
//
// A line that opened a block is followed to the brace that closes it. That
// block belonged to the statement that failed, and reading it as statements of
// the block around it puts every brace after it out of step: one mistake in an
// `if let` line became a second message about a `let` three lines down, saying
// a file holds `module`, `import` and `fn` — in the middle of a function.
static void recover_statement(Parser *parser) {
    uint32_t depth = 0;
    while (!check(parser, KEST_TOK_EOF)) {
        if (check(parser, KEST_TOK_LBRACE)) {
            depth++;
            advance(parser);
            continue;
        }
        if (check(parser, KEST_TOK_RBRACE)) {
            if (depth == 0) {
                break;
            }
            depth--;
            advance(parser);
            continue;
        }
        if (check(parser, KEST_TOK_NEWLINE)) {
            advance(parser);
            if (depth == 0) {
                break;
            }
            continue;
        }
        if (depth == 0 && starts_statement(peek(parser).kind)) {
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
    if (found.kind == KEST_TOK_SEMICOLON) {
        error_at(parser, found.span, "K0105",
                 "statements are not separated by `;`");
        kest_diags_suggest(parser->diags, "remove it; a line break ends a "
                                          "statement");
        advance(parser);
        recover_statement(parser);
        return;
    }
    error_at(parser, found.span, "K0201", "expected end of line, found %s",
             kest_token_name(found.kind));
    // A statement that begins with a word this language nearly has is a
    // misspelt keyword, and the message above is about the token after it —
    // which is the one thing in the line that is not wrong. So the word is
    // pointed at as well.
    if (parser->began_with.kind == KEST_TOK_IDENT) {
        const char *nearly = kest_nearest_keyword(
            span_text(parser, parser->began_with.span),
            parser->began_with.span.length);
        if (nearly != NULL) {
            kest_diags_note(parser->diags, parser->source,
                            parser->began_with.span, "did you mean `%s`?",
                            nearly);
        }
    }
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

// `store<ref<Npc>>` ends in one token that is two closers. The first half
// closes this type and the second is left where it is, so the type around it
// closes on what is still a `>`.
static void close_generic(Parser *parser) {
    if (check(parser, KEST_TOK_GTGT)) {
        KestToken *token = &parser->tokens[parser->position];
        token->kind = KEST_TOK_GT;
        token->span.offset++;
        token->span.length--;
        return;
    }
    expect(parser, KEST_TOK_GT);
}

static KestTypeRef *parse_type(Parser *parser) {
    KestSpan start = current_span(parser);
    KestTypeRef *type = KEST_ARENA_NEW(parser->arena, KestTypeRef);
    if (type == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }

    if (check(parser, KEST_TOK_FN)) {
        advance(parser);
        type->kind = KEST_TYPE_FN;
        if (!expect(parser, KEST_TOK_LPAREN)) {
            return NULL;
        }
        List args = {0};
        if (!check(parser, KEST_TOK_RPAREN)) {
            do {
                KestTypeRef *param = parse_type(parser);
                if (param == NULL) {
                    return NULL;
                }
                list_push(parser, &args, param);
            } while (match(parser, KEST_TOK_COMMA));
        }
        expect(parser, KEST_TOK_RPAREN);
        type->args = (KestTypeRef **)args.items;
        type->arg_count = args.count;
        if (match(parser, KEST_TOK_ARROW)) {
            type->element = parse_type(parser);
            if (type->element == NULL) {
                return NULL;
            }
        }
        // The same words a declaration uses, because it is the same promise.
        if (is_word(parser, 0, "no") && peek_at(parser, 1).kind == KEST_TOK_DOT &&
            is_word(parser, 2, "alloc")) {
            advance(parser);
            advance(parser);
            advance(parser);
            type->no_alloc = true;
        }
        type->span =
            span_between(start, parser->tokens[parser->position - 1].span);
        return type;
    }

    if (match(parser, KEST_TOK_LBRACKET)) {
        type->kind = KEST_TYPE_ARRAY;
        type->element = parse_type(parser);
        // `[f32; 16]` is that many, where it stands. `[f32]` is a handle to
        // something that can grow.
        if (match(parser, KEST_TOK_SEMICOLON)) {
            // A number, or the name of a constant that is one. Which it is,
            // is the type layer's to say: it is the thing that can work a
            // constant out.
            type->count = current_span(parser);
            if (!match(parser, KEST_TOK_IDENT)) {
                expect(parser, KEST_TOK_INT);
            }
        }
        expect(parser, KEST_TOK_RBRACKET);
    } else if (check(parser, KEST_TOK_IDENT)) {
        type->kind = KEST_TYPE_NAMED;
        // A type from another module is one name with a dot in it, the same
        // way a call into one is.
        type->name = parse_path(parser);
        if (match(parser, KEST_TOK_LT)) {
            type->kind = KEST_TYPE_GENERIC;
            List args = {0};
            do {
                list_push(parser, &args, parse_type(parser));
            } while (match(parser, KEST_TOK_COMMA));
            close_generic(parser);
            type->args = (KestTypeRef **)args.items;
            type->arg_count = args.count;
        }
    } else {
        KestToken found = peek(parser);
        error_at(parser, found.span, "K0203", "expected a type, found %s",
                 kest_token_name(found.kind));
        // Which of the three it is, because what somebody wrote says which
        // language they came from and each has a different answer here. See
        // D515.
        if (found.kind == KEST_TOK_STAR || found.kind == KEST_TOK_AMP) {
            kest_diags_suggest(parser->diags,
                               "there are no pointers here: what names a slot "
                               "in a store is `ref<T>`");
        } else if (found.kind == KEST_TOK_LPAREN) {
            kest_diags_suggest(parser->diags,
                               "there are no tuples here: a `struct` is what "
                               "holds several things");
        } else {
            kest_diags_suggest(parser->diags,
                               "a type is a name, `[T]`, `[T; N]` or "
                               "`fn(...)`, and `?` after any of them");
        }
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
static KestExpr *parse_match(Parser *parser);
static KestExpr *parse_if(Parser *parser);
static bool parse_block(Parser *parser, KestBlock *block);

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

// Finds the `}` that closes a hole. Braces nest and a string inside a hole
// may hold either brace, so both are followed rather than counted blindly.
static uint32_t close_of_hole(Parser *parser, uint32_t open, uint32_t end) {
    const char *text = parser->source->text;
    uint32_t depth = 0;
    for (uint32_t i = open; i < end; i++) {
        if (text[i] == '\\') {
            i++;
        } else if (text[i] == '"') {
            for (i++; i < end && text[i] != '"'; i++) {
                if (text[i] == '\\') {
                    i++;
                }
            }
        } else if (text[i] == '{') {
            depth++;
        } else if (text[i] == '}') {
            if (--depth == 0) {
                return i;
            }
        }
    }
    return end;
}

// A string with `{}` in it is a run of pieces rather than one value. The holes
// are parsed from the source they were written in, so a mistake inside one
// reports where it is.
static KestExpr *parse_string(Parser *parser, KestSpan span) {
    uint32_t start = span.offset + 1;
    uint32_t end = span.offset + span.length - 1;

    bool interpolated = false;
    for (uint32_t i = start; i < end; i++) {
        if (parser->source->text[i] == '\\') {
            i++;
        } else if (parser->source->text[i] == '{') {
            interpolated = true;
            break;
        }
    }
    if (!interpolated) {
        return new_expr(parser, KEST_EXPR_STRING, span);
    }

    List parts = {0};
    uint32_t chunk = start;
    for (uint32_t i = start; i < end; i++) {
        if (parser->source->text[i] == '\\') {
            i++;
            continue;
        }
        if (parser->source->text[i] != '{') {
            continue;
        }

        uint32_t close = close_of_hole(parser, i, end);
        KestTextPart *literal = KEST_ARENA_NEW(parser->arena, KestTextPart);
        KestTextPart *hole = KEST_ARENA_NEW(parser->arena, KestTextPart);
        if (literal == NULL || hole == NULL) {
            parser->out_of_memory = true;
            return NULL;
        }

        literal->text.offset = chunk;
        literal->text.length = i - chunk;
        if (literal->text.length > 0) {
            list_push(parser, &parts, literal);
        }

        if (close == end || close == i + 1) {
            KestSpan where = {i, close == end ? 1 : 2};
            error_at(parser, where, "K0207",
                     close == end ? "this hole is not closed"
                                  : "this hole is empty");
            return NULL;
        }

        uint32_t count = 0;
        KestToken *tokens = kest_lex_range(parser->arena, parser->source,
                                           parser->diags, i + 1, close, &count);
        if (tokens == NULL) {
            parser->out_of_memory = true;
            return NULL;
        }
        Parser inner = *parser;
        inner.tokens = tokens;
        inner.count = count;
        inner.position = 0;
        inner.recovering = false;
        hole->value = parse_expr(&inner);
        parser->out_of_memory = inner.out_of_memory;
        if (hole->value == NULL) {
            return NULL;
        }
        list_push(parser, &parts, hole);

        i = close;
        chunk = close + 1;
    }

    if (chunk < end) {
        KestTextPart *tail = KEST_ARENA_NEW(parser->arena, KestTextPart);
        if (tail == NULL) {
            parser->out_of_memory = true;
            return NULL;
        }
        tail->text.offset = chunk;
        tail->text.length = end - chunk;
        list_push(parser, &parts, tail);
    }

    KestExpr *expr = new_expr(parser, KEST_EXPR_TEXT, span);
    if (expr == NULL) {
        return NULL;
    }
    // The list holds pointers; the tree holds the parts themselves, so a
    // reader of the tree does not chase one pointer per character run.
    KestTextPart *flat =
        KEST_ARENA_ARRAY(parser->arena, KestTextPart, parts.count + 1);
    if (flat == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }
    for (uint32_t i = 0; i < parts.count; i++) {
        flat[i] = *(KestTextPart *)parts.items[i];
    }
    expr->text.parts = flat;
    expr->text.count = parts.count;
    return expr;
}

// `match` is one thing whether it is used for its value or for what its arms
// do, so it is parsed once, here, and a statement that is a match is a match
// that was not used for anything.
// Everything up to the `}` that closes the arms, for an arm nothing could
// read. Without it the arms under a bad one are read as statements, and a
// reader who wrote one thing wrong is told about three. See D512.
static void skip_arms(Parser *parser) {
    uint32_t depth = 1;
    while (depth > 0 && !check(parser, KEST_TOK_EOF)) {
        KestTokenKind kind = advance(parser).kind;
        if (kind == KEST_TOK_LBRACE) {
            depth++;
        } else if (kind == KEST_TOK_RBRACE) {
            depth--;
        }
    }
}

static KestExpr *parse_match(Parser *parser) {
    KestSpan start = advance(parser).span;

    // One subject, or several answered together. The list stops at the brace,
    // because no expression in the grammar begins with one.
    List subjects = {0};
    do {
        KestExpr *subject = parse_expr(parser);
        if (subject == NULL) {
            return NULL;
        }
        list_push(parser, &subjects, subject);
    } while (match(parser, KEST_TOK_COMMA));
    if (!expect(parser, KEST_TOK_LBRACE)) {
        return NULL;
    }

    List arms = {0};
    bool gives = false;
    bool blocks = false;
    skip_newlines(parser);
    while (!check(parser, KEST_TOK_RBRACE) && !check(parser, KEST_TOK_EOF)) {
        KestArm *arm = KEST_ARENA_NEW(parser->arena, KestArm);
        if (arm == NULL) {
            parser->out_of_memory = true;
            return NULL;
        }
        KestSpan arm_start = current_span(parser);

        List parts = {0};
        do {
            KestArmPart *part = KEST_ARENA_NEW(parser->arena, KestArmPart);
            if (part == NULL) {
                parser->out_of_memory = true;
                return NULL;
            }
            // `else` is the position with no case, which is the only way to
            // leave one out.
            if (!match(parser, KEST_TOK_ELSE)) {
                part->name = current_span(parser);
                if (!expect(parser, KEST_TOK_IDENT)) {
                    // The rule behind the expectation. Somebody who has met a
                    // language where `match` chooses between values writes a
                    // number or `true` here and hears about a token. What it
                    // chooses between is said where the subject is read, and
                    // the subject is not read until the arms parse. See D512.
                    kest_diags_suggest(parser->diags,
                                       "a `match` arm names a case of an "
                                       "enum, and `else` answers the rest");
                    skip_arms(parser);
                    return NULL;
                }
                if (match(parser, KEST_TOK_LPAREN)) {
                    List names = {0};
                    if (!check(parser, KEST_TOK_RPAREN)) {
                        do {
                            KestSpan *held =
                                KEST_ARENA_NEW(parser->arena, KestSpan);
                            if (held == NULL) {
                                parser->out_of_memory = true;
                                return NULL;
                            }
                            *held = current_span(parser);
                            if (!expect(parser, KEST_TOK_IDENT)) {
                                skip_arms(parser);
                                return NULL;
                            }
                            list_push(parser, &names, held);
                        } while (match(parser, KEST_TOK_COMMA));
                    }
                    expect(parser, KEST_TOK_RPAREN);
                    part->bindings =
                        KEST_ARENA_ARRAY(parser->arena, KestSpan,
                                         names.count == 0 ? 1 : names.count);
                    if (part->bindings == NULL) {
                        parser->out_of_memory = true;
                        return NULL;
                    }
                    for (uint32_t i = 0; i < names.count; i++) {
                        part->bindings[i] = *(KestSpan *)names.items[i];
                    }
                    part->binding_count = names.count;
                }
            }
            list_push(parser, &parts, part);
        } while (match(parser, KEST_TOK_COMMA));

        arm->parts = KEST_ARENA_ARRAY(parser->arena, KestArmPart,
                                      parts.count == 0 ? 1 : parts.count);
        if (arm->parts == NULL) {
            parser->out_of_memory = true;
            return NULL;
        }
        for (uint32_t i = 0; i < parts.count; i++) {
            arm->parts[i] = *(KestArmPart *)parts.items[i];
        }
        arm->part_count = parts.count;
        arm->span =
            span_between(arm_start, parser->tokens[parser->position - 1].span);

        if (match(parser, KEST_TOK_ARROW)) {
            gives = true;
            arm->value = parse_expr(parser);
            if (arm->value == NULL) {
                return NULL;
            }
        } else {
            blocks = true;
            if (!parse_block(parser, &arm->body)) {
                return NULL;
            }
        }
        list_push(parser, &arms, arm);
        end_statement(parser);
        skip_newlines(parser);
        if (parser->out_of_memory) {
            return NULL;
        }
    }
    KestSpan close = current_span(parser);
    expect(parser, KEST_TOK_RBRACE);

    if (gives && blocks) {
        error_at(parser, start, "K0208",
                 "every arm gives a value or none does");
        kest_diags_suggest(parser->diags,
                           "an arm gives one with `-> value` and does "
                           "something with a block");
    }

    KestExpr *expr = new_expr(parser, KEST_EXPR_MATCH, span_between(start, close));
    if (expr == NULL) {
        return NULL;
    }
    expr->choose.subjects = (KestExpr **)subjects.items;
    expr->choose.subject_count = subjects.count;
    expr->choose.gives = gives;
    expr->choose.arms =
        KEST_ARENA_ARRAY(parser->arena, KestArm, arms.count == 0 ? 1 : arms.count);
    if (expr->choose.arms == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }
    for (uint32_t i = 0; i < arms.count; i++) {
        expr->choose.arms[i] = *(KestArm *)arms.items[i];
    }
    expr->choose.arm_count = arms.count;
    return expr;
}

// `if let` and `while let` give a name to what an optional holds. They are not
// patterns: nothing is compared and nothing is taken apart, so the only thing
// between `let` and `=` is a name. Said here because the expectation on its own
// is a message about a token, and what a reader who wrote `if let Some(x) =`
// needs is the rule. See D513.
static bool parse_binding(Parser *parser, KestSpan *name, const char *what) {
    *name = current_span(parser);
    if (!expect(parser, KEST_TOK_IDENT)) {
        kest_diags_suggest(parser->diags,
                           "`%s let` names what is held rather than comparing "
                           "with it", what);
        return false;
    }
    if (!expect(parser, KEST_TOK_EQ)) {
        kest_diags_suggest(parser->diags,
                           "`%s let` names what an optional holds: "
                           "`%s let held = ...`", what, what);
        return false;
    }
    return true;
}

// Like `match`, parsed once whether it is used for its value or for what its
// arms do. An arm that gives one says so with `->`.
static KestExpr *parse_if(Parser *parser) {
    KestSpan start = advance(parser).span;
    KestBranch branch = {0};

    if (match(parser, KEST_TOK_LET) &&
        !parse_binding(parser, &branch.binding, "if")) {
        return NULL;
    }
    if (wrapped_whole(parser)) {
        refuse_wrapped(parser, "if");
        return NULL;
    }
    // The condition stops at the brace or the arrow on its own: no expression
    // in the grammar begins with either.
    branch.condition = parse_expr(parser);
    if (branch.condition == NULL) {
        return NULL;
    }

    bool blocks = false;
    if (match(parser, KEST_TOK_ARROW)) {
        branch.gives = true;
        branch.then_value = parse_expr(parser);
        if (branch.then_value == NULL) {
            return NULL;
        }
    } else {
        blocks = true;
        if (!parse_block(parser, &branch.then_body)) {
            return NULL;
        }
    }

    // An `if` that gives a value is one expression and may be written over two
    // lines, because the one it is written on may not be long enough. Nothing
    // else can follow a value with `else`, so looking past the break for it
    // takes nothing away from anybody.
    if (branch.gives && check(parser, KEST_TOK_NEWLINE)) {
        uint32_t ahead = 1;
        while (peek_at(parser, ahead).kind == KEST_TOK_NEWLINE) {
            ahead++;
        }
        if (peek_at(parser, ahead).kind == KEST_TOK_ELSE) {
            skip_newlines(parser);
        }
    }

    if (match(parser, KEST_TOK_ELSE)) {
        branch.has_else = true;
        if (check(parser, KEST_TOK_IF)) {
            branch.otherwise = parse_if(parser);
            if (branch.otherwise == NULL) {
                return NULL;
            }
            if (branch.otherwise->branch->gives) {
                branch.gives = true;
            } else {
                blocks = true;
            }
        } else if (match(parser, KEST_TOK_ARROW)) {
            branch.gives = true;
            branch.else_value = parse_expr(parser);
            if (branch.else_value == NULL) {
                return NULL;
            }
        } else {
            blocks = true;
            if (!parse_block(parser, &branch.else_body)) {
                return NULL;
            }
        }
    }

    KestSpan whole =
        span_between(start, parser->tokens[parser->position - 1].span);
    if (branch.gives && blocks) {
        error_at(parser, whole, "K0208",
                 "every arm gives a value or none does");
        kest_diags_suggest(parser->diags,
                           "an arm gives one with `-> value` and does "
                           "something with a block");
    }

    KestExpr *expr = new_expr(parser, KEST_EXPR_IF, whole);
    if (expr == NULL) {
        return NULL;
    }
    KestBranch *held = kest_arena_alloc(parser->arena, sizeof *held, _Alignof(KestBranch));
    if (held == NULL) {
        parser->out_of_memory = true;
        return NULL;
    }
    *held = branch;
    expr->branch = held;
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
        return parse_string(parser, token.span);
    case KEST_TOK_BYTE:
        advance(parser);
        return new_expr(parser, KEST_EXPR_BYTE, token.span);
    case KEST_TOK_IDENT:
        advance(parser);
        return new_expr(parser, KEST_EXPR_NAME, token.span);
    case KEST_TOK_NONE:
        advance(parser);
        return new_expr(parser, KEST_EXPR_NONE, token.span);
    case KEST_TOK_MATCH:
        return parse_match(parser);
    case KEST_TOK_IF:
        return parse_if(parser);
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
    case KEST_TOK_LBRACKET: {
        // Only a primary can start with a bracket, because indexing needs an
        // expression in front of it, so nothing has to be disambiguated.
        advance(parser);
        List items = {0};
        if (!check(parser, KEST_TOK_RBRACKET)) {
            do {
                KestExpr *item = parse_expr(parser);
                if (item == NULL) {
                    return NULL;
                }
                list_push(parser, &items, item);
            } while (match(parser, KEST_TOK_COMMA));
        }
        KestSpan close = current_span(parser);
        expect(parser, KEST_TOK_RBRACKET);

        KestExpr *array = new_expr(parser, KEST_EXPR_ARRAY,
                                   span_between(token.span, close));
        if (array == NULL) {
            return NULL;
        }
        array->array.items = (KestExpr **)items.items;
        array->array.count = items.count;
        return array;
    }
    default:
        error_at(parser, token.span, "K0204", "expected an expression, found %s",
                 kest_token_name(token.kind));
        // A block where a value was wanted, which is what somebody writes who
        // has met a language whose blocks are expressions. Here a block is a
        // statement and an `if` is the one thing that gives a value out of
        // arms. See D515.
        if (token.kind == KEST_TOK_LBRACE) {
            kest_diags_suggest(parser->diags,
                               "a block is not a value: an `if` gives one "
                               "with `->`");
        }
        return NULL;
    }
}

// What a type is made of, so that `store<Node>()` can be told apart from
// `a < b > (c)`. Anything else between the angles stops the scan, and what
// stops it stays the comparison it reads as.
static bool part_of_a_type(KestTokenKind kind) {
    switch (kind) {
    case KEST_TOK_IDENT:
    case KEST_TOK_INT:
    case KEST_TOK_LBRACKET:
    case KEST_TOK_RBRACKET:
    case KEST_TOK_COMMA:
    case KEST_TOK_SEMICOLON:
    case KEST_TOK_QUESTION:
    case KEST_TOK_DOT:
    case KEST_TOK_LT:
    case KEST_TOK_GT:
        return true;
    default:
        return false;
    }
}

// How many tokens `<...>` takes when a call follows it, and nought when this
// is a comparison like any other. The name and the `<` have to be written
// against each other, which is how somebody writes a type argument and not
// how anybody writes a comparison.
static uint32_t type_arguments(Parser *parser, const KestExpr *name) {
    if (name->kind != KEST_EXPR_NAME || !check(parser, KEST_TOK_LT) ||
        name->span.offset + name->span.length != peek(parser).span.offset) {
        return 0;
    }
    uint32_t depth = 0;
    for (uint32_t ahead = 0; ahead < 32; ahead++) {
        KestTokenKind kind = peek_at(parser, ahead).kind;
        if (!part_of_a_type(kind)) {
            return 0;
        }
        if (kind == KEST_TOK_LT) {
            depth++;
        } else if (kind == KEST_TOK_GT && --depth == 0) {
            return peek_at(parser, ahead + 1).kind == KEST_TOK_LPAREN
                       ? ahead + 1
                       : 0;
        }
    }
    return 0;
}

// Calls, field access and indexing, which bind tighter than any operator.
static KestExpr *parse_postfix(Parser *parser) {
    KestExpr *expr = parse_primary(parser);
    if (expr == NULL) {
        return NULL;
    }

    while (true) {
        // A type written at a call is read as two comparisons and refused at
        // the `)`, which is nowhere near what is wrong. This language takes
        // types from what is passed, or from what a binding is written as,
        // and that is worth saying where it was written.
        uint32_t angles = type_arguments(parser, expr);
        if (angles > 0) {
            KestSpan written = span_between(
                expr->span, peek_at(parser, angles - 1).span);
            bool nothing_passed =
                peek_at(parser, angles + 1).kind == KEST_TOK_RPAREN;
            bool said = !parser->recovering;
            error_at(parser, written, "K0211",
                     "`%.*s` is not given its types where it is called",
                     (int)expr->span.length, span_text(parser, expr->span));
            if (said) {
                // Which way the type gets there depends on whether anything
                // is passed, and naming the wrong one of the two is worse
                // than naming neither.
                kest_diags_suggest(
                    parser->diags,
                    nothing_passed
                        ? "write `%.*s()`, and the type on the binding it "
                          "goes to"
                        : "write `%.*s(...)`: the copy is made from what is "
                          "passed",
                    (int)expr->span.length, span_text(parser, expr->span));
            }
            for (uint32_t i = 0; i < angles; i++) {
                advance(parser);
            }
            continue;
        }
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
                // `t.0` is what somebody writes who has met tuples. There are
                // none here: what a struct holds is named. See D514.
                kest_diags_suggest(parser->diags,
                                   "a field is named, so there is nothing at "
                                   "a position to read");
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
    if (check(parser, KEST_TOK_MINUS) || check(parser, KEST_TOK_BANG) ||
        check(parser, KEST_TOK_TILDE)) {
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

// The bitwise operators bind tighter than the comparisons, which is the one
// place C is known to be wrong: `flags & MASK == 0` reads as one thing and
// means another there. Shifts keep C's place, above the bitwise operators and
// below the arithmetic, because `1 << n + 1` has never been the trap.
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
    case KEST_TOK_PIPE:
        return 5;
    case KEST_TOK_CARET:
        return 6;
    case KEST_TOK_AMP:
        return 7;
    case KEST_TOK_LTLT:
    case KEST_TOK_GTGT:
        return 8;
    case KEST_TOK_PLUS:
    case KEST_TOK_MINUS:
        return 9;
    case KEST_TOK_STAR:
    case KEST_TOK_SLASH:
    case KEST_TOK_PERCENT:
        return 10;
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
    parser->began_with = peek(parser);

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
            // The rule behind the expectation, which is the part worth
            // hearing: a name is given its value where it is written, and
            // there is no declaring one now and filling it in later. See D506.
            kest_diags_suggest(parser->diags,
                               "a `let` gives its value where it is written");
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

    // `defer f(x)` runs when the block it is in ends, however it ends. It
    // takes a call and not a statement: a block would want its own scope
    // rules and nothing has asked for one.
    if (match(parser, KEST_TOK_DEFER)) {
        KestStmt *stmt = new_stmt(parser, KEST_STMT_DEFER, start);
        if (stmt == NULL) {
            return NULL;
        }
        // A block is what a reader who has met another language writes here,
        // and it is the one thing the expression parser has nothing to say
        // about: it would report a `{` where an expression was wanted, which
        // is the token and not the rule. The rule is the same one below.
        if (check(parser, KEST_TOK_LBRACE)) {
            error_at(parser, current_span(parser), "K0210",
                     "a `defer` runs something, and this is not a call");
            return NULL;
        }
        stmt->value = parse_expr(parser);
        if (stmt->value == NULL) {
            return NULL;
        }
        if (stmt->value->kind != KEST_EXPR_CALL) {
            error_at(parser, stmt->value->span, "K0210",
                     "a `defer` runs something, and this is not a call");
        }
        stmt->span =
            span_between(start, parser->tokens[parser->position - 1].span);
        return stmt;
    }

    if (match(parser, KEST_TOK_WHILE)) {
        // `while let one = next()` runs while there is something, the same
        // way `if let` runs when there is.
        KestSpan binding = {0, 0};
        if (match(parser, KEST_TOK_LET) &&
            !parse_binding(parser, &binding, "while")) {
            return NULL;
        }
        if (wrapped_whole(parser)) {
            refuse_wrapped(parser, "while");
            return NULL;
        }
        KestExpr *condition = parse_expr(parser);
        if (condition == NULL) {
            return NULL;
        }
        KestStmt *stmt = new_stmt(parser, KEST_STMT_WHILE, start);
        if (stmt == NULL) {
            return NULL;
        }
        stmt->loop.binding = binding;
        stmt->loop.condition = condition;
        parse_block(parser, &stmt->loop.body);
        stmt->span =
            span_between(start, parser->tokens[parser->position - 1].span);
        return stmt;
    }

    if (match(parser, KEST_TOK_FOR)) {
        KestSpan index = {0, 0};
        KestSpan name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT)) {
            // The same rule in the third place it is written: what stands
            // here is a name for what comes out, and there is nothing else it
            // could be. See D513.
            kest_diags_suggest(parser->diags,
                               "a `for` names what it walks over: "
                               "`for one in ...`");
            return NULL;
        }
        // `for i, x in a`: the position first, because that is the order it
        // is asked for in.
        if (match(parser, KEST_TOK_COMMA)) {
            index = name;
            name = current_span(parser);
            if (!expect(parser, KEST_TOK_IDENT)) {
                kest_diags_suggest(parser->diags,
                                   "a `for` names the position first and what "
                                   "it walks over second: `for at, one in ...`");
                return NULL;
            }
        }
        if (!expect(parser, KEST_TOK_IN)) {
            return NULL;
        }
        KestExpr *sequence = parse_expr(parser);
        if (sequence == NULL) {
            return NULL;
        }
        // `from..to` is a way to write a walk rather than a value, so it is
        // read here and nowhere else.
        KestExpr *until = NULL;
        if (match(parser, KEST_TOK_DOTDOT)) {
            until = parse_expr(parser);
            if (until == NULL) {
                return NULL;
            }
        }
        KestStmt *stmt = new_stmt(parser, KEST_STMT_FOR, start);
        if (stmt == NULL) {
            return NULL;
        }
        stmt->each.index = index;
        stmt->each.name = name;
        stmt->each.sequence = sequence;
        stmt->each.until = until;
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

    uint32_t compound_at = 0;
    const char *compound = no_compound(parser, &compound_at);
    if (compound != NULL) {
        error_at(parser, peek_at(parser, compound_at).span, "K0214",
                 "`%s=` is not one of the four this language has", compound);
        kest_diags_suggest(parser->diags,
                           "they are `+=`, `-=`, `*=` and `/=`; write it out: "
                           "`x = x %s y`",
                           compound);
        return NULL;
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
        // `x i32` is what somebody writes who has met Go, and the token this
        // wanted says nothing about which of the two orders is right. See
        // D514.
        kest_diags_suggest(parser->diags,
                           "a field is written `name: type`");
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

// `fn sort<T>` and `struct Pair<A, B>` ask for the same thing, so they are
// read the same way. A name here stands for one type per copy.
static bool parse_type_params(Parser *parser, KestDecl *decl) {
    if (!match(parser, KEST_TOK_LT)) {
        return true;
    }
    List names = {0};
    do {
        KestSpan *held = KEST_ARENA_NEW(parser->arena, KestSpan);
        if (held == NULL) {
            parser->out_of_memory = true;
            return false;
        }
        *held = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT)) {
            return false;
        }
        list_push(parser, &names, held);
    } while (match(parser, KEST_TOK_COMMA));
    close_generic(parser);

    decl->type_params = KEST_ARENA_ARRAY(parser->arena, KestSpan,
                                         names.count == 0 ? 1 : names.count);
    if (decl->type_params == NULL) {
        parser->out_of_memory = true;
        return false;
    }
    for (uint32_t i = 0; i < names.count; i++) {
        decl->type_params[i] = *(KestSpan *)names.items[i];
    }
    decl->type_param_count = names.count;
    return true;
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

    if (!parse_type_params(parser, decl)) {
        return NULL;
    }
    if (is_extern && decl->type_param_count > 0) {
        error_at(parser, decl->name, "K0209",
                 "an extern function is the host's, so it takes no types");
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
            // `const N = 1` is what a reader writes first, and the rule it
            // meets is the one above this line: the type is written because
            // the name crosses a boundary. See D514.
            kest_diags_suggest(parser->diags,
                               "a `const` is written with its type: "
                               "`const N: i32 = 1`");
            return NULL;
        }
        decl->constant.type = parse_type(parser);
        if (!expect(parser, KEST_TOK_EQ)) {
            kest_diags_suggest(parser->diags,
                               "a `const` gives its value where it is "
                               "written");
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
        if (!parse_type_params(parser, decl) ||
            !expect(parser, KEST_TOK_LBRACE)) {
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

    if (match(parser, KEST_TOK_ENUM)) {
        KestDecl *decl = new_decl(parser, KEST_DECL_ENUM, start);
        if (decl == NULL) {
            return NULL;
        }
        decl->name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT) ||
            !expect(parser, KEST_TOK_LBRACE)) {
            return NULL;
        }

        List cases = {0};
        skip_newlines(parser);
        while (!check(parser, KEST_TOK_RBRACE) && !check(parser, KEST_TOK_EOF)) {
            KestVariant *variant = KEST_ARENA_NEW(parser->arena, KestVariant);
            if (variant == NULL) {
                parser->out_of_memory = true;
                return NULL;
            }
            variant->name = current_span(parser);
            if (!expect(parser, KEST_TOK_IDENT)) {
                return NULL;
            }
            // What a case carries is a list of types by position, the way
            // what it is built with is a list of values by position.
            if (match(parser, KEST_TOK_LPAREN)) {
                List types = {0};
                if (!check(parser, KEST_TOK_RPAREN)) {
                    do {
                        KestTypeRef *type = parse_type(parser);
                        if (type == NULL) {
                            return NULL;
                        }
                        list_push(parser, &types, type);
                    } while (match(parser, KEST_TOK_COMMA));
                }
                expect(parser, KEST_TOK_RPAREN);
                variant->payload = (KestTypeRef **)types.items;
                variant->payload_count = types.count;
            }
            list_push(parser, &cases, variant);
            end_statement(parser);
            skip_newlines(parser);
            if (parser->out_of_memory) {
                return decl;
            }
        }
        KestSpan close = current_span(parser);
        expect(parser, KEST_TOK_RBRACE);

        decl->choice.cases = (KestVariant **)cases.items;
        decl->choice.case_count = cases.count;
        decl->span = span_between(start, close);
        return decl;
    }

    // `flags` is a word rather than a keyword: it means a declaration only
    // where one begins, and everywhere else it is a name, so `npc.flags` and
    // a module called `flags` keep working. `no.alloc` is read the same way.
    if (is_word(parser, 0, "flags") &&
        peek_at(parser, 1).kind == KEST_TOK_IDENT &&
        peek_at(parser, 2).kind == KEST_TOK_COLON) {
        advance(parser);
        KestDecl *decl = new_decl(parser, KEST_DECL_FLAGS, start);
        if (decl == NULL) {
            return NULL;
        }
        decl->name = current_span(parser);
        if (!expect(parser, KEST_TOK_IDENT) || !expect(parser, KEST_TOK_COLON)) {
            return NULL;
        }
        // The width is what a host sees, so it is written rather than counted
        // off the names: a ninth flag has to be a decision, not a surprise.
        decl->choice.width = parse_type(parser);
        if (decl->choice.width == NULL || !expect(parser, KEST_TOK_LBRACE)) {
            return NULL;
        }

        List cases = {0};
        skip_newlines(parser);
        while (!check(parser, KEST_TOK_RBRACE) && !check(parser, KEST_TOK_EOF)) {
            KestVariant *variant = KEST_ARENA_NEW(parser->arena, KestVariant);
            if (variant == NULL) {
                parser->out_of_memory = true;
                return NULL;
            }
            variant->name = current_span(parser);
            if (!expect(parser, KEST_TOK_IDENT)) {
                return NULL;
            }
            list_push(parser, &cases, variant);
            end_statement(parser);
            skip_newlines(parser);
            if (parser->out_of_memory) {
                return decl;
            }
        }
        KestSpan close = current_span(parser);
        expect(parser, KEST_TOK_RBRACE);

        decl->choice.cases = (KestVariant **)cases.items;
        decl->choice.case_count = cases.count;
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

    // A flag set that does not say how wide it is. It is refused either way;
    // what changes is whether the reader is told which of the eight things a
    // file holds they nearly wrote.
    if (is_word(parser, 0, "flags") && peek_at(parser, 1).kind == KEST_TOK_IDENT &&
        peek_at(parser, 2).kind == KEST_TOK_LBRACE) {
        KestSpan name = peek_at(parser, 1).span;
        error_at(parser, peek(parser).span, "K0212",
                 "a flag set says how wide it is");
        kest_diags_suggest(parser->diags,
                           "the width is what a host sees, so it is written "
                           "rather than counted off the names: `flags %.*s: "
                           "u8 {`",
                           (int)name.length, span_text(parser, name));
        return NULL;
    }

    // `flags` beginning a declaration that neither arm above could read.
    // Without this it falls through to the end, where the caret sits on the
    // word `flags` under a line saying that a file holds `flags`. See D514.
    if (is_word(parser, 0, "flags")) {
        advance(parser);
        if (expect(parser, KEST_TOK_IDENT)) {
            KestToken after = peek(parser);
            error_at(parser, after.span, "K0201", "expected `:`, found %s",
                     kest_token_name(after.kind));
            kest_diags_suggest(parser->diags,
                               "a flag set says how wide it is: "
                               "`flags Name: u8 {`");
        }
        return NULL;
    }

    KestToken found = peek(parser);
    error_at(parser, found.span, "K0202",
             "expected a declaration, found %s", kest_token_name(found.kind));
    const char *nearly =
        found.kind == KEST_TOK_IDENT
            ? kest_nearest_keyword(span_text(parser, found.span),
                                   found.span.length)
            : NULL;
    if (nearly != NULL) {
        kest_diags_suggest(parser->diags, "did you mean `%s`?", nearly);
    } else {
        kest_diags_suggest(parser->diags,
                           "a file holds `module`, `import`, `const`, "
                           "`struct`, `enum`, `flags`, `fn` and `extern fn`");
    }
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
