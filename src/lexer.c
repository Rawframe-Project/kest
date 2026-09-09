#include "lexer.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *text;
    KestTokenKind kind;
} Keyword;

static const Keyword KEYWORDS[] = {
    {"break", KEST_TOK_BREAK}, {"const", KEST_TOK_CONST},
    {"continue", KEST_TOK_CONTINUE}, {"defer", KEST_TOK_DEFER},
    {"else", KEST_TOK_ELSE}, {"enum", KEST_TOK_ENUM},
    {"extern", KEST_TOK_EXTERN}, {"false", KEST_TOK_FALSE},
    {"fn", KEST_TOK_FN}, {"for", KEST_TOK_FOR},
    {"if", KEST_TOK_IF}, {"import", KEST_TOK_IMPORT},
    {"in", KEST_TOK_IN}, {"let", KEST_TOK_LET},
    {"match", KEST_TOK_MATCH}, {"module", KEST_TOK_MODULE},
    {"none", KEST_TOK_NONE}, {"return", KEST_TOK_RETURN},
    {"struct", KEST_TOK_STRUCT}, {"true", KEST_TOK_TRUE},
    {"while", KEST_TOK_WHILE},
};

static const char *const TOKEN_NAMES[] = {
    "end of file", "end of line", "identifier", "integer",  "float",
    "string",      "byte",        "`break`",     "`const`",    "`continue`",
    "`defer`",     "`else`",
    "`enum`",      "`extern`",    "`false`",    "`fn`",     "`for`",
    "`if`",        "`import`",    "`in`",       "`let`",    "`match`",
    "`module`",    "`none`",
    "`return`",    "`struct`",    "`true`",     "`while`",  "`(`",  "`)`",
    "`{`",         "`}`",         "`[`",        "`]`",      "`,`",
    "`;`",
    "`.`",         "`..`",        "`:`",        "`?`",      "`->`",
    "`=`",
    "`==`",        "`!=`",        "`<`",        "`<=`",     "`>`",
    "`>=`",        "`+`",         "`-`",        "`*`",      "`/`",
    "`%`",         "`!`",         "`&&`",       "`||`",     "`&`",
    "`|`",         "`^`",         "`~`",        "`<<`",     "`>>`",
    "`+=`",        "`-=`",        "`*=`",       "`/=`",     "invalid token",
};

// What an escape is written as and what it stands for. One list: what a piece
// of text may hold, what a byte written on its own may hold, what either turns
// into, and the message that names them are all read from here. A `default`
// beside a few cases is how a ninth would arrive without anybody deciding what
// it means.
static const struct {
    char written;
    char means;
} ESCAPES[] = {
    {'n', '\n'}, {'t', '\t'},   {'r', '\r'}, {'\\', '\\'},
    {'"', '"'}, {'{', '{'}, {'}', '}'}, {'0', '\0'},
};

// What it stands for, or NULL for a character that is not one of them.
static const char *escape_means(char written) {
    for (size_t i = 0; i < sizeof(ESCAPES) / sizeof(ESCAPES[0]); i++) {
        if (ESCAPES[i].written == written) {
            return &ESCAPES[i].means;
        }
    }
    return NULL;
}

// The list a reader is given when they write one that is not there, built
// from the same table rather than written out beside it.
static const char *escapes_written(KestArena *arena) {
    size_t room = sizeof(ESCAPES) / sizeof(ESCAPES[0]) * 4 + 1;
    char *out = kest_arena_alloc(arena, room, 1);
    if (out == NULL) {
        return "";
    }
    size_t used = 0;
    for (size_t i = 0; i < sizeof(ESCAPES) / sizeof(ESCAPES[0]); i++) {
        if (i > 0) {
            out[used++] = ' ';
        }
        out[used++] = '\\';
        out[used++] = ESCAPES[i].written;
    }
    out[used] = '\0';
    return out;
}

const char *kest_literal_text(KestArena *arena, const KestSource *source,
                              KestSpan span) {
    const char *raw = source->text + span.offset;
    size_t length = span.length;

    char *text = kest_arena_alloc(arena, length + 1, 1);
    if (text == NULL) {
        return "";
    }

    size_t used = 0;
    for (size_t i = 0; i < length; i++) {
        if (raw[i] != '\\' || i + 1 == length) {
            text[used++] = raw[i];
            continue;
        }
        i++;
        const char *stands_for = escape_means(raw[i]);
        // One that is not an escape was refused where it was read, and what
        // is written here is what somebody wrote: a message about it says so
        // and this is not the place to say it twice.
        text[used++] = stands_for == NULL ? raw[i] : *stands_for;
    }
    text[used] = '\0';
    return text;
}

double kest_literal_real(const KestSource *source, KestSpan span) {
    char buffer[64];
    size_t length = span.length < sizeof(buffer) - 1 ? span.length : 0;
    memcpy(buffer, source->text + span.offset, length);
    buffer[length] = '\0';
    return strtod(buffer, NULL);
}

// One name a kind, and the compiler counts them. `check-tables.sh` holds the
// two to saying the same thing; this holds them to being the same length,
// which is the half that can be caught while building.
_Static_assert(sizeof(TOKEN_NAMES) / sizeof(TOKEN_NAMES[0]) ==
                   KEST_TOK_ERROR + 1,
               "every token kind has a name and nothing else does");

// The keyword a word was nearly, or nothing when it was near none of them. A
// misspelt keyword is a name as far as the lexer is concerned, and what
// happens next is a message about the token after it, so the parser asks this
// before it says anything.
//
// The limit is the one every suggestion in this compiler uses: a third of what
// was written, and nothing under three letters, because `in`, `if` and `fn`
// are one edit from most short words.
const char *kest_nearest_keyword(const char *name, size_t length) {
    if (length < 3) {
        return NULL;
    }
    uint32_t limit = length == 3 ? 1 : (uint32_t)length / 3;
    const char *best = NULL;
    uint32_t nearest = limit + 1;
    for (size_t i = 0; i < sizeof(KEYWORDS) / sizeof(KEYWORDS[0]); i++) {
        uint32_t distance = kest_word_distance(name, length, KEYWORDS[i].text,
                                               strlen(KEYWORDS[i].text), limit);
        if (distance < nearest) {
            nearest = distance;
            best = KEYWORDS[i].text;
        }
    }
    return best;
}

// A comment runs to the end of its line, and a string may hold two slashes
// that begin nothing, which is the only reason this is not a search. The
// lexer throws them away — they are not tokens — and this is where anything
// that wants them asks, so that the formatter and a tool reading a file are
// not two answers to one question.
uint32_t kest_comments(const KestSource *source, KestSpan *into,
                       uint32_t room) {
    const char *text = source->text;
    size_t length = source->length;
    uint32_t found = 0;

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
        // A comment ends where the line does, and a line ends with one
        // character here and two on a machine that writes both. The first of
        // the two is where the comment stops either way: the return was never
        // something somebody wrote in it, and a file that ends its lines with
        // one of those and nothing else is a file this would otherwise read
        // as one comment from the first `//` to the end.
        size_t end = i;
        while (end < length && text[end] != '\n' && text[end] != '\r') {
            end++;
        }
        if (into != NULL && found < room) {
            KestSpan span = {(uint32_t)i, (uint32_t)(end - i)};
            into[found] = span;
        }
        found++;
        i = end;
    }
    return found;
}

const char *kest_token_name(KestTokenKind kind) {
    return TOKEN_NAMES[kind];
}

static void kest_lexer_init(KestLexer *lexer, const KestSource *source,
                     KestDiags *diags) {
    lexer->source = source;
    lexer->diags = diags;
    lexer->offset = 0;
    lexer->bracket_depth = 0;
    lexer->previous = KEST_TOK_NEWLINE;
    // Which nothing set, so it was whatever the stack held. The one thing
    // that read it was a suggestion — a file-level escape mistake could be
    // told that a hole holds code — so the mistake it made was to say
    // something wrong to somebody now and then, and a wrong suggestion costs
    // more than none. Nothing here catches an unset field: the sanitisers
    // this project builds under do not read memory that was never written.
    lexer->in_hole = false;
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
// A newline after one of these ends the statement, and after anything else it
// does not. It is a list of what a value can end with, and every kind there is
// appears in it: a token kind added without a decision about this reads as an
// unfinished line and swallows the next one, which has happened twice.
bool kest_lexer_ends_statement(KestTokenKind kind) {
    switch (kind) {
    case KEST_TOK_IDENT:
    case KEST_TOK_INT:
    case KEST_TOK_FLOAT:
    case KEST_TOK_STRING:
    case KEST_TOK_BYTE:
    case KEST_TOK_TRUE:
    case KEST_TOK_FALSE:
    case KEST_TOK_RPAREN:
    case KEST_TOK_RBRACE:
    case KEST_TOK_RBRACKET:
    case KEST_TOK_RETURN:
    case KEST_TOK_BREAK:
    case KEST_TOK_CONTINUE:
    case KEST_TOK_QUESTION:
    case KEST_TOK_NONE:
    // A type that takes types ends in one: `ref<Npc>` and `store<Job>` are
    // what a field is written as, and a field ends where its line does. A
    // comparison written with its right side on the next line is the price,
    // and it is refused where it is written rather than read as two things.
    case KEST_TOK_GT:
        return true;

    // Everything else, written out rather than left to a `default`, because a
    // token kind added without a decision about this reads as an unfinished
    // line and swallows the next one. That has happened twice: `byte` and
    // `>`. Now the build stops until somebody says which of the two it is.
    case KEST_TOK_EOF:
    case KEST_TOK_NEWLINE:
    case KEST_TOK_CONST:
    case KEST_TOK_DEFER:
    case KEST_TOK_ELSE:
    case KEST_TOK_ENUM:
    case KEST_TOK_EXTERN:
    case KEST_TOK_FN:
    case KEST_TOK_FOR:
    case KEST_TOK_IF:
    case KEST_TOK_IMPORT:
    case KEST_TOK_IN:
    case KEST_TOK_LET:
    case KEST_TOK_MATCH:
    case KEST_TOK_MODULE:
    case KEST_TOK_STRUCT:
    case KEST_TOK_WHILE:
    case KEST_TOK_LPAREN:
    case KEST_TOK_LBRACE:
    case KEST_TOK_LBRACKET:
    case KEST_TOK_COMMA:
    case KEST_TOK_SEMICOLON:
    case KEST_TOK_DOT:
    case KEST_TOK_DOTDOT:
    case KEST_TOK_COLON:
    case KEST_TOK_ARROW:
    case KEST_TOK_EQ:
    case KEST_TOK_EQEQ:
    case KEST_TOK_BANGEQ:
    case KEST_TOK_LT:
    case KEST_TOK_LTEQ:
    case KEST_TOK_GTEQ:
    case KEST_TOK_PLUS:
    case KEST_TOK_MINUS:
    case KEST_TOK_STAR:
    case KEST_TOK_SLASH:
    case KEST_TOK_PERCENT:
    case KEST_TOK_BANG:
    case KEST_TOK_AMPAMP:
    case KEST_TOK_PIPEPIPE:
    case KEST_TOK_AMP:
    case KEST_TOK_PIPE:
    case KEST_TOK_CARET:
    case KEST_TOK_TILDE:
    case KEST_TOK_LTLT:
    case KEST_TOK_GTGT:
    case KEST_TOK_PLUSEQ:
    case KEST_TOK_MINUSEQ:
    case KEST_TOK_STAREQ:
    case KEST_TOK_SLASHEQ:
    case KEST_TOK_ERROR:
        return false;
    }
    // Nothing reaches this: every kind there is, is above. C wants a value.
    return false;
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
    // A hole may hold a string of its own, so the quote that ends this one is
    // the one found outside every brace.
    uint32_t depth = 0;

    while (true) {
        char c = at(lexer, 0);
        if (c == '"' && depth == 0) {
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
            // A nought is a byte like any other and text is not: text ends at
            // its first one, so a piece of it with one in the middle is a
            // piece that says less than it holds. The machine refuses one
            // that arrives from an array or from a host; this is the third
            // way in, and the only one that can be refused where it is
            // written.
            if (escape == '0') {
                kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0110",
                               span_from(lexer->offset, lexer->offset + 2),
                               "a zero byte inside text, and text ends at a "
                               "zero byte");
                kest_diags_suggest(lexer->diags,
                                   "hold bytes in a `[u8]` when one of them is "
                                   "nought; `'\\0'` is that byte on its own");
            }
            if (escape == '\0' || escape_means(escape) == NULL) {
                kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0103",
                               span_from(lexer->offset, lexer->offset + 2),
                               "unknown escape sequence `\\%c`", escape);
                kest_diags_suggest(lexer->diags, "known escapes are %s",
                                   escapes_written(lexer->diags->arena));
            }
            lexer->offset += 2;
            continue;
        }
        // A byte that ends a line on another machine, written inside text as
        // itself. It is a byte like any other once the program runs, and it
        // is one nobody reading the file can see: two pieces of text that are
        // not the same look the same, and a file that crossed machines has
        // one in it without anybody having written it.
        if (c == '\r') {
            kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0109",
                           span_from(lexer->offset, lexer->offset + 1),
                           "a carriage return inside text, written as itself");
            kest_diags_suggest(lexer->diags,
                               "write `\\r`, which is the same byte and can "
                               "be read");
        }
        if (c == '{') {
            depth++;
        } else if (c == '}' && depth > 0) {
            depth--;
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
            // Not inside a hole. A hole is code written inside text, and the
            // formatter prints it from what it means rather than copying it,
            // so a comment in one is a comment nothing can put back. Nothing
            // can read it either: at the level of the file the whole string
            // is one token, so no tool is told there is a comment there. A
            // comment nobody can read and nothing can keep is not a comment.
            // See D392.
            uint32_t said = lexer->offset;
            while (at(lexer, 0) != '\n' && at(lexer, 0) != '\r' &&
                   at(lexer, 0) != '\0' &&
                   !(lexer->in_hole && at(lexer, 0) == '}')) {
                lexer->offset++;
            }
            if (lexer->in_hole) {
                kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0111",
                               span_from(said, lexer->offset),
                               "a comment inside a hole");
                kest_diags_suggest(lexer->diags,
                                   "a hole holds code and is written back "
                                   "from what it means, so a comment in one "
                                   "is kept by nothing and read by nobody: "
                                   "write it above the line");
            }
        } else {
            return;
        }
    }
}

static KestToken kest_lexer_next(KestLexer *lexer) {
    while (true) {
        skip_blanks(lexer);

        uint32_t start = lexer->offset;
        char c = at(lexer, 0);

        if (c == '\n') {
            lexer->offset++;
            if (lexer->bracket_depth == 0 &&
                kest_lexer_ends_statement(lexer->previous)) {
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

        // `'a'` is one byte written the way it reads. Text is its bytes
        // (D021), so this is not a character type: it is a `u8` and anything
        // that is not exactly one byte is refused.
        if (c == '\'') {
            lexer->offset++;
            while (at(lexer, 0) != '\'' && at(lexer, 0) != '\n' &&
                   at(lexer, 0) != '\0') {
                if (at(lexer, 0) == '\\' && at(lexer, 1) != '\0') {
                    // The same escapes text has, said the same way: a byte
                    // written on its own and a byte written in a piece of
                    // text are one spelling, and one spelling is one list.
                    if (escape_means(at(lexer, 1)) == NULL) {
                        kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR,
                                       "K0103",
                                       span_from(lexer->offset,
                                                 lexer->offset + 2),
                                       "unknown escape sequence `\\%c`",
                                       at(lexer, 1));
                        kest_diags_suggest(lexer->diags,
                                           "known escapes are %s",
                                           escapes_written(
                                               lexer->diags->arena));
                    }
                    lexer->offset++;
                } else if (at(lexer, 0) == '\r') {
                    kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0109",
                                   span_from(lexer->offset, lexer->offset + 1),
                                   "a carriage return inside text, written as "
                                   "itself");
                    kest_diags_suggest(lexer->diags,
                                       "write `\\r`, which is the same byte "
                                       "and can be read");
                }
                lexer->offset++;
            }
            if (at(lexer, 0) != '\'') {
                kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0106",
                               span_from(start, lexer->offset),
                               "this byte has no closing quote");
                return make(lexer, KEST_TOK_ERROR, start);
            }
            lexer->offset++;
            return make(lexer, KEST_TOK_BYTE, start);
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
            if (next == '.') {
                lexer->offset++;
                return make(lexer, KEST_TOK_DOTDOT, start);
            }
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
            if (next == '<') {
                lexer->offset++;
                return make(lexer, KEST_TOK_LTLT, start);
            }
            return make(lexer, KEST_TOK_LT, start);
        case '>':
            if (next == '=') {
                lexer->offset++;
                return make(lexer, KEST_TOK_GTEQ, start);
            }
            if (next == '>') {
                lexer->offset++;
                return make(lexer, KEST_TOK_GTGT, start);
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
            return make(lexer, KEST_TOK_AMP, start);
        case '|':
            if (next == '|') {
                lexer->offset++;
                return make(lexer, KEST_TOK_PIPEPIPE, start);
            }
            return make(lexer, KEST_TOK_PIPE, start);
        case '^':
            return make(lexer, KEST_TOK_CARET, start);
        case '~':
            return make(lexer, KEST_TOK_TILDE, start);
        case ';':
            // Read as a token and refused where it is written. Whether a `;`
            // is a mistake depends on where it is, and that is the parser's
            // to know: inside `[f32; 16]` it separates a count.
            return make(lexer, KEST_TOK_SEMICOLON, start);
        default:
            break;
        }

        kest_diags_add(lexer->diags, KEST_SEVERITY_ERROR, "K0102",
                       span_from(start, lexer->offset),
                       "unexpected character `%.*s`",
                       (int)(lexer->offset - start), lexer->source->text + start);
        if (lexer->source->text[start] == '\\') {
            // The mistake everybody makes once: escaping a quote inside a
            // hole. What is in one is code, so a string in it is written the
            // way a string is written anywhere.
            kest_diags_suggest(lexer->diags,
                               lexer->in_hole
                                   ? "a hole holds code, so a string inside "
                                     "one needs no escape: `{f(\"x\")}`"
                                   : "an escape is written inside text, and "
                                     "this is not inside any");
        }
        return make(lexer, KEST_TOK_ERROR, start);
    }
}

static KestToken *lex_from(KestArena *arena, KestLexer *lexer, uint32_t end,
                           uint32_t *count);

typedef struct {
    uint32_t from;
    uint32_t to;
    const char *what;
} Unseen;

// Characters that are in a file without being on the screen. A name may hold
// any character the writer's language has, and none of these are one: they
// either look like a space and are not one, or take no room at all, or say
// which way the rest of the line is to be read.
static const Unseen UNSEEN[] = {
    {0x00a0, 0x00a0, "a space that is not the space"},
    {0x00ad, 0x00ad, "a hyphen that is not shown"},
    {0x1680, 0x1680, "a space that is not the space"},
    {0x2000, 0x200a, "a space that is not the space"},
    {0x200b, 0x200d, "a mark with no width"},
    {0x200e, 0x200f, "a mark saying which way to read"},
    {0x2028, 0x2029, "a line break that no line ends with"},
    {0x202a, 0x202e, "a mark saying which way to read"},
    {0x202f, 0x202f, "a space that is not the space"},
    {0x205f, 0x205f, "a space that is not the space"},
    {0x2060, 0x2064, "a mark with no width"},
    {0x2066, 0x2069, "a mark saying which way to read"},
    {0x3000, 0x3000, "a space that is not the space"},
    {0xfeff, 0xfeff, "a mark with no width"},
};

static const char *unseen_what(uint32_t code) {
    for (size_t i = 0; i < sizeof(UNSEEN) / sizeof(UNSEEN[0]); i++) {
        if (code >= UNSEEN[i].from && code <= UNSEEN[i].to) {
            return UNSEEN[i].what;
        }
    }
    return NULL;
}

// How many bytes the character starting at `at` is written in, and which
// character it is. Nought is a byte that starts no character: a lead byte with
// the wrong bits, a sequence cut short, a character written in more bytes than
// it needs, half of a surrogate pair, or a number past the last character
// there is.
static uint32_t decoded(const char *text, uint32_t length, uint32_t at,
                        uint32_t *code) {
    unsigned char lead = (unsigned char)text[at];
    uint32_t width = 0;
    uint32_t value = 0;
    if ((lead & 0xe0) == 0xc0) {
        width = 2;
        value = lead & 0x1fu;
    } else if ((lead & 0xf0) == 0xe0) {
        width = 3;
        value = lead & 0x0fu;
    } else if ((lead & 0xf8) == 0xf0) {
        width = 4;
        value = lead & 0x07u;
    } else {
        return 0;
    }

    if (at + width > length) {
        return 0;
    }
    for (uint32_t i = 1; i < width; i++) {
        unsigned char next = (unsigned char)text[at + i];
        if ((next & 0xc0) != 0x80) {
            return 0;
        }
        value = (value << 6) | (next & 0x3fu);
    }

    static const uint32_t LEAST[] = {0, 0, 0x80, 0x800, 0x10000};
    if (value < LEAST[width] || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff)) {
        return 0;
    }

    *code = value;
    return width;
}

// What the whole file is made of, before anything is made of the file. A
// program is read by people as well as by this, and the two have to be reading
// the same thing: a byte that is no character at all, or a character that is
// in the file without being on the screen, is where they stop.
static void check_text(const KestSource *source, KestDiags *diags) {
    uint32_t at = 0;
    while (at < source->length) {
        if ((unsigned char)source->text[at] < 0x80) {
            at++;
            continue;
        }

        uint32_t code = 0;
        uint32_t width = decoded(source->text, (uint32_t)source->length, at,
                                 &code);
        if (width == 0) {
            KestSpan span = {at, 1};
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0107", span,
                           "the byte `0x%02x` starts no character",
                           (unsigned char)source->text[at]);
            kest_diags_suggest(diags,
                               "a file this language reads is UTF-8 throughout");
            at++;
            continue;
        }

        const char *what = unseen_what(code);
        if (what != NULL) {
            KestSpan span = {at, width};
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0108", span,
                           "`U+%04X` is %s", code, what);
            if (code == 0xfeff && at == 0) {
                kest_diags_suggest(diags,
                                   "a file here is UTF-8 already: save it "
                                   "without the mark");
            } else {
                kest_diags_suggest(diags, "take it out: what a file looks "
                                          "like is what it is");
            }
        }
        at += width;
    }
}


KestToken *kest_lex_range(KestArena *arena, const KestSource *source,
                          KestDiags *diags, uint32_t start, uint32_t end,
                          uint32_t *count) {
    KestLexer lexer;
    kest_lexer_init(&lexer, source, diags);
    lexer.offset = start;
    // The only range anything asks for is the inside of a hole.
    lexer.in_hole = true;
    return lex_from(arena, &lexer, end, count);
}

KestToken *kest_lex_all(KestArena *arena, const KestSource *source,
                        KestDiags *diags, uint32_t *count) {
    check_text(source, diags);

    KestLexer lexer;
    kest_lexer_init(&lexer, source, diags);
    return lex_from(arena, &lexer, (uint32_t)source->length, count);
}

static KestToken *lex_from(KestArena *arena, KestLexer *lexer, uint32_t end,
                           uint32_t *count) {

    KestToken *tokens = NULL;
    uint32_t used = 0;
    uint32_t capacity = 0;

    while (true) {
        if (used == capacity) {
            uint32_t grown = capacity == 0 ? 256 : capacity * 2;
            KestToken *moved = KEST_ARENA_ARRAY(arena, KestToken, grown);
            if (moved == NULL) {
                return NULL;
            }
            if (used > 0) {
                memcpy(moved, tokens, sizeof(KestToken) * used);
            }
            tokens = moved;
            capacity = grown;
        }

        if (lexer->offset >= end) {
            KestSpan stop = {end, 0};
            KestToken done = {KEST_TOK_EOF, stop};
            tokens[used++] = done;
            break;
        }
        tokens[used] = kest_lexer_next(lexer);
        if (tokens[used++].kind == KEST_TOK_EOF) {
            break;
        }
    }

    *count = used;
    return tokens;
}

uint64_t kest_token_integer(const char *text, size_t length, bool *overflow) {
    uint64_t value = 0;
    uint64_t base = 10;
    size_t at = 0;
    if (length > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        at = 2;
    }
    *overflow = false;
    for (; at < length; at++) {
        char c = text[at];
        uint64_t digit = c <= '9' ? (uint64_t)(c - '0')
                                  : (uint64_t)((c | 0x20) - 'a' + 10);
        if (value > (UINT64_MAX - digit) / base) {
            *overflow = true;
            return UINT64_MAX;
        }
        value = value * base + digit;
    }
    return value;
}
