#ifndef KEST_LEXER_H
#define KEST_LEXER_H

#include "diag.h"

typedef enum {
    KEST_TOK_EOF,
    // A statement terminator. Emitted for a line break only where a statement
    // could actually have ended; see kest_lexer_next.
    KEST_TOK_NEWLINE,

    KEST_TOK_IDENT,
    KEST_TOK_INT,
    KEST_TOK_FLOAT,
    KEST_TOK_STRING,
    KEST_TOK_BYTE,

    KEST_TOK_BREAK,
    KEST_TOK_CONST,
    KEST_TOK_CONTINUE,
    KEST_TOK_DEFER,
    KEST_TOK_TYPE,
    KEST_TOK_ELSE,
    KEST_TOK_ENUM,
    KEST_TOK_EXTERN,
    KEST_TOK_FALSE,
    KEST_TOK_FN,
    KEST_TOK_FOR,
    KEST_TOK_IF,
    KEST_TOK_IMPORT,
    KEST_TOK_IN,
    KEST_TOK_LET,
    KEST_TOK_MATCH,
    KEST_TOK_MODULE,
    KEST_TOK_NONE,
    KEST_TOK_RETURN,
    KEST_TOK_STRUCT,
    KEST_TOK_TRUE,
    KEST_TOK_WHILE,

    KEST_TOK_LPAREN,
    KEST_TOK_RPAREN,
    KEST_TOK_LBRACE,
    KEST_TOK_RBRACE,
    KEST_TOK_LBRACKET,
    KEST_TOK_RBRACKET,
    KEST_TOK_COMMA,
    KEST_TOK_SEMICOLON,
    KEST_TOK_DOT,
    KEST_TOK_DOTDOT,
    KEST_TOK_COLON,
    KEST_TOK_QUESTION,
    KEST_TOK_ARROW,

    KEST_TOK_EQ,
    KEST_TOK_EQEQ,
    KEST_TOK_BANGEQ,
    KEST_TOK_LT,
    KEST_TOK_LTEQ,
    KEST_TOK_GT,
    KEST_TOK_GTEQ,
    KEST_TOK_PLUS,
    KEST_TOK_MINUS,
    KEST_TOK_STAR,
    KEST_TOK_SLASH,
    KEST_TOK_PERCENT,
    KEST_TOK_BANG,
    KEST_TOK_AMPAMP,
    KEST_TOK_PIPEPIPE,
    KEST_TOK_AMP,
    KEST_TOK_PIPE,
    KEST_TOK_CARET,
    KEST_TOK_TILDE,
    KEST_TOK_LTLT,
    KEST_TOK_GTGT,
    KEST_TOK_PLUSEQ,
    KEST_TOK_MINUSEQ,
    KEST_TOK_STAREQ,
    KEST_TOK_SLASHEQ,

    // Produced where a diagnostic was already recorded, so the parser can keep
    // going without reporting the same byte twice.
    KEST_TOK_ERROR,
} KestTokenKind;

typedef struct {
    KestTokenKind kind;
    KestSpan span;
} KestToken;

typedef struct {
    const KestSource *source;
    KestDiags *diags;
    uint32_t offset;
    // Line breaks inside brackets continue the statement, so they are not
    // terminators. Braces do not count: a block holds statements.
    uint32_t bracket_depth;
    KestTokenKind previous;
} KestLexer;

// Tokenises the whole source into arena memory. The parser needs to look
// further ahead than one token, and a file's token count is bounded by its
// size, so there is nothing to stream.
KestToken *kest_lex_all(KestArena *arena, const KestSource *source,
                        KestDiags *diags, uint32_t *count);

// Tokenises one region of the source. The offsets a token carries are into
// the whole file either way, so what comes back from inside a string reports
// at the place it was written.
KestToken *kest_lex_range(KestArena *arena, const KestSource *source,
                          KestDiags *diags, uint32_t start, uint32_t end,
                          uint32_t *count);

// The value an integer literal spells. Sets `overflow` when it does not fit
// in sixty-four bits, which is the widest anything here can be.
uint64_t kest_token_integer(const char *text, size_t length, bool *overflow);

// The spelling used in diagnostics: `fn`, `identifier`, `end of file`.
const char *kest_token_name(KestTokenKind kind);

#endif
