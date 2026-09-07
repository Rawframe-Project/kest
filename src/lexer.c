#include "lexer.h"

#include <string.h>

typedef struct {
    const char *text;
    KestTokenKind kind;
} Keyword;

static const Keyword KEYWORDS[] = {
    {"break", KEST_TOK_BREAK},       {"const", KEST_TOK_CONST},
    {"continue", KEST_TOK_CONTINUE}, {"else", KEST_TOK_ELSE},
    {"extern", KEST_TOK_EXTERN},     {"false", KEST_TOK_FALSE},
    {"fn", KEST_TOK_FN},             {"for", KEST_TOK_FOR},
    {"if", KEST_TOK_IF},             {"import", KEST_TOK_IMPORT},
    {"in", KEST_TOK_IN},             {"let", KEST_TOK_LET},
    {"module", KEST_TOK_MODULE},     {"return", KEST_TOK_RETURN},
    {"struct", KEST_TOK_STRUCT},     {"true", KEST_TOK_TRUE},
    {"while", KEST_TOK_WHILE},
};

static const char *const TOKEN_NAMES[] = {
    "end of file", "end of line", "identifier", "integer",  "float",
    "string",      "`break`",     "`const`",    "`continue`", "`else`",
    "`extern`",    "`false`",     "`fn`",       "`for`",    "`if`",
    "`import`",    "`in`",        "`let`",      "`module`", "`return`",
    "`struct`",    "`true`",      "`while`",    "`(`",      "`)`",
    "`{`",         "`}`",         "`[`",        "`]`",      "`,`",
    "`.`",         "`:`",         "`?`",        "`->`",     "`=`",
    "`==`",        "`!=`",        "`<`",        "`<=`",     "`>`",
    "`>=`",        "`+`",         "`-`",        "`*`",      "`/`",
    "`%`",         "`!`",         "`&&`",       "`||`",     "`+=`",
    "`-=`",        "`*=`",        "`/=`",       "invalid token",
};

const char *kest_token_name(KestTokenKind kind) {
    return TOKEN_NAMES[kind];
}

void kest_lexer_init(KestLexer *lexer, const KestSource *source,
                     KestDiags *diags) {
    lexer->source = source;
    lexer->diags = diags;
    lexer->offset = 0;
    lexer->bracket_depth = 0;
    lexer->previous = KEST_TOK_NEWLINE;
}

static char at(const KestLexer *lexer, uint32_t ahead) {
    uint32_t index = lexer->offset + ahead;
    if (index >= lexer->source->length) {
        return '\0';
    }
    return lexer->source->text[index];
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_hex_digit(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Any byte above ASCII starts or continues an identifier, which makes every
// UTF-8 sequence legal in a name without carrying Unicode tables.
static bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           (unsigned char)c >= 0x80;
}

static bool is_ident_part(char c) {
    return is_ident_start(c) || is_digit(c);
}

static KestSpan span_from(uint32_t start, uint32_t end) {
    KestSpan span = {start, end - start};
    return span;
}

static KestToken make(KestLexer *lexer, KestTokenKind kind, uint32_t start) {
    lexer->previous = kind;
    KestToken token = {kind, span_from(start, lexer->offset)};
    return token;
}

// Whether a line break after this token ends a statement. A break after an
// operator, an opening bracket or a comma is a continuation, because the
// statement cannot have finished there.
static bool ends_statement(KestTokenKind kind) {
    switch (kind) {
    case KEST_TOK_IDENT:
    case KEST_TOK_INT:
    case KEST_TOK_FLOAT:
    case KEST_TOK_STRING:
    case KEST_TOK_TRUE:
    case KEST_TOK_FALSE:
    case KEST_TOK_RPAREN:
    case KEST_TOK_RBRACE:
    case KEST_TOK_RBRACKET:
    case KEST_TOK_RETURN:
    case KEST_TOK_BREAK:
    case KEST_TOK_CONTINUE:
    case KEST_TOK_QUESTION:
        return true;
    default:
        return false;
    }
}

static KestToken scan_ident(KestLexer *lexer, uint32_t start) {
    while (is_ident_part(at(lexer, 0))) {
        lexer->offset++;
    }

    size_t length = lexer->offset - start;
    const char *text = lexer->source->text + start;
    for (size_t i = 0; i < sizeof(KEYWORDS) / sizeof(KEYWORDS[0]); i++) {
        if (strlen(KEYWORDS[i].text) == length &&
            memcmp(KEYWORDS[i].text, text, length) == 0) {
            return make(lexer, KEYWORDS[i].kind, start);
        }
    }
    return make(lexer, KEST_TOK_IDENT, start);
}

static KestToken scan_number(KestLexer *lexer, uint32_t start) {
    if (at(lexer, 0) == '0' && (at(lexer, 1) == 'x' || at(lexer, 1) == 'X')) {
        lexer->offset += 2;
        if (!is_hex_digit(at(lexer, 0))) {
            kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0104",
                           span_from(start, lexer->offset),
                           "hexadecimal literal has no digits");
            return make(lexer, KEST_TOK_ERROR, start);
        }
        while (is_hex_digit(at(lexer, 0))) {
            lexer->offset++;
        }
        return make(lexer, KEST_TOK_INT, start);
    }

    while (is_digit(at(lexer, 0))) {
        lexer->offset++;
    }

    bool is_float = false;
    if (at(lexer, 0) == '.' && is_digit(at(lexer, 1))) {
        is_float = true;
        lexer->offset++;
        while (is_digit(at(lexer, 0))) {
            lexer->offset++;
        }
    }

    if (at(lexer, 0) == 'e' || at(lexer, 0) == 'E') {
        uint32_t exponent = lexer->offset;
        lexer->offset++;
        if (at(lexer, 0) == '+' || at(lexer, 0) == '-') {
            lexer->offset++;
        }
        if (!is_digit(at(lexer, 0))) {
            // Back out, so `1e` reads as the number 1 followed by the name e
            // and the parser reports what is actually wrong there.
            lexer->offset = exponent;
        } else {
            is_float = true;
            while (is_digit(at(lexer, 0))) {
                lexer->offset++;
            }
        }
    }

    return make(lexer, is_float ? KEST_TOK_FLOAT : KEST_TOK_INT, start);
}

static KestToken scan_string(KestLexer *lexer, uint32_t start) {
    lexer->offset++;
    while (true) {
        char c = at(lexer, 0);
        if (c == '"') {
            lexer->offset++;
            return make(lexer, KEST_TOK_STRING, start);
        }
        if (c == '\0' || c == '\n') {
            kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0101",
                           span_from(start, start + 1),
                           "string is not terminated");
            kest_diags_suggest(lexer->diags, "add a closing `\"`");
            return make(lexer, KEST_TOK_ERROR, start);
        }
        if (c == '\\') {
            char escape = at(lexer, 1);
            if (strchr("ntr\\\"{}0", escape) == NULL || escape == '\0') {
                kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0103",
                               span_from(lexer->offset, lexer->offset + 2),
                               "unknown escape sequence `\\%c`", escape);
                kest_diags_suggest(lexer->diags,
                                   "known escapes are \\n \\t \\r \\\\ \\\" "
                                   "\\{ \\} \\0");
            }
            lexer->offset += 2;
            continue;
        }
        lexer->offset++;
    }
}

// Skips spaces, tabs and comments. Line breaks are left for the caller,
// because whether they matter depends on the previous token.
static void skip_blanks(KestLexer *lexer) {
    while (true) {
        char c = at(lexer, 0);
        if (c == ' ' || c == '\t' || c == '\r') {
            lexer->offset++;
        } else if (c == '/' && at(lexer, 1) == '/') {
            while (at(lexer, 0) != '\n' && at(lexer, 0) != '\0') {
                lexer->offset++;
            }
        } else {
            return;
        }
    }
}

KestToken kest_lexer_next(KestLexer *lexer) {
    while (true) {
        skip_blanks(lexer);

        uint32_t start = lexer->offset;
        char c = at(lexer, 0);

        if (c == '\n') {
            lexer->offset++;
            if (lexer->bracket_depth == 0 && ends_statement(lexer->previous)) {
                return make(lexer, KEST_TOK_NEWLINE, start);
            }
            continue;
        }

        if (c == '\0') {
            return make(lexer, KEST_TOK_EOF, start);
        }

        if (is_ident_start(c)) {
            return scan_ident(lexer, start);
        }

        if (is_digit(c)) {
            return scan_number(lexer, start);
        }

        if (c == '"') {
            return scan_string(lexer, start);
        }

        lexer->offset++;
        char next = at(lexer, 0);

        switch (c) {
        case '(':
            lexer->bracket_depth++;
            return make(lexer, KEST_TOK_LPAREN, start);
        case '[':
            lexer->bracket_depth++;
            return make(lexer, KEST_TOK_LBRACKET, start);
        case ')':
            if (lexer->bracket_depth > 0) {
                lexer->bracket_depth--;
            }
            return make(lexer, KEST_TOK_RPAREN, start);
        case ']':
            if (lexer->bracket_depth > 0) {
                lexer->bracket_depth--;
            }
            return make(lexer, KEST_TOK_RBRACKET, start);
        case '{':
            return make(lexer, KEST_TOK_LBRACE, start);
        case '}':
            return make(lexer, KEST_TOK_RBRACE, start);
        case ',':
            return make(lexer, KEST_TOK_COMMA, start);
        case '.':
            return make(lexer, KEST_TOK_DOT, start);
        case ':':
            return make(lexer, KEST_TOK_COLON, start);
        case '?':
            return make(lexer, KEST_TOK_QUESTION, start);
        case '%':
            return make(lexer, KEST_TOK_PERCENT, start);
        case '=':
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_EQEQ, start);
            }
            return make(lexer, KEST_TOK_EQ, start);
        case '!':
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_BANGEQ, start);
            }
            return make(lexer, KEST_TOK_BANG, start);
        case '<':
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_LTEQ, start);
            }
            return make(lexer, KEST_TOK_LT, start);
        case '>':
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_GTEQ, start);
            }
            return make(lexer, KEST_TOK_GT, start);
        case '+':
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_PLUSEQ, start);
            }
            return make(lexer, KEST_TOK_PLUS, start);
        case '-':
            if (next == '>') {
                lexer->offset++;
                return make(lexer, KEST_TOK_ARROW, start);
            }
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_MINUSEQ, start);
            }
            return make(lexer, KEST_TOK_MINUS, start);
        case '*':
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_STAREQ, start);
            }
            return make(lexer, KEST_TOK_STAR, start);
        case '/':
            if (next == '*') {
                kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0106",
                               span_from(start, start + 2),
                               "block comments are not part of the language");
                kest_diags_suggest(lexer->diags, "use `//` on each line");
                lexer->offset++;
                return make(lexer, KEST_TOK_ERROR, start);
            }
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_SLASHEQ, start);
            }
            return make(lexer, KEST_TOK_SLASH, start);
        case '&':
            if (next == '&') {
                lexer->offset++;
                return make(lexer, KEST_TOK_AMPAMP, start);
            }
            break;
        case '|':
            if (next == '|') {
                lexer->offset++;
                return make(lexer, KEST_TOK_PIPEPIPE, start);
            }
            break;
        case ';':
            kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0105",
                           span_from(start, lexer->offset),
                           "statements are not separated by `;`");
            kest_diags_suggest(lexer->diags, "remove it; a line break ends a "
                                             "statement");
            return make(lexer, KEST_TOK_ERROR, start);
        default:
            break;
        }

        kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0102",
                       span_from(start, lexer->offset),
                       "unexpected character `%.*s`",
                       (int)(lexer->offset - start), lexer->source->text + start);
        return make(lexer, KEST_TOK_ERROR, start);
    }
}
