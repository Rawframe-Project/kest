#ifndef KEST_AST_H
#define KEST_AST_H

#include "lexer.h"

// Literals and names keep only their span. The text is source[span], and the
// value is produced later, so the tree stays small and every node can report
// where it came from.

typedef enum {
    KEST_TYPE_NAMED,    // f32, Player
    KEST_TYPE_GENERIC,  // ref<Npc>
    KEST_TYPE_ARRAY,    // [T]
    KEST_TYPE_OPTIONAL, // T?
} KestTypeKind;

typedef struct KestTypeRef KestTypeRef;

struct KestTypeRef {
    KestTypeKind kind;
    KestSpan span;
    // NAMED and GENERIC only.
    KestSpan name;
    KestTypeRef **args;
    uint32_t arg_count;
    // ARRAY and OPTIONAL only.
    KestTypeRef *element;
};

typedef enum {
    KEST_EXPR_INT,
    KEST_EXPR_FLOAT,
    KEST_EXPR_STRING,
    KEST_EXPR_BOOL,
    KEST_EXPR_NAME,
    KEST_EXPR_UNARY,
    KEST_EXPR_BINARY,
    KEST_EXPR_CALL,
    KEST_EXPR_FIELD,
    KEST_EXPR_INDEX,
    KEST_EXPR_ARRAY,
    KEST_EXPR_NONE,
    KEST_EXPR_TEXT,
} KestExprKind;

typedef struct KestExpr KestExpr;

// One piece of an interpolated string: either a run of characters or the
// expression written in a hole, never both.
typedef struct {
    KestSpan text;
    KestExpr *value;
} KestTextPart;

// Resolved by the checker. The compiler reads it to choose between an integer
// and a floating point instruction, rather than working the type out again.
typedef struct KestType KestType;

struct KestExpr {
    KestExprKind kind;
    KestSpan span;
    KestType *type;
    // Set by the checker where a plain value stands in a place that wants an
    // optional. The compiler then writes the tag beside it. Nothing else in
    // the language converts on its own.
    bool wrapped;
    union {
        bool boolean;
        struct {
            KestTokenKind op;
            KestExpr *operand;
        } unary;
        struct {
            KestTokenKind op;
            KestExpr *left;
            KestExpr *right;
        } binary;
        struct {
            KestExpr *callee;
            KestExpr **args;
            uint32_t arg_count;
        } call;
        struct {
            KestExpr *object;
            KestSpan name;
        } field;
        struct {
            KestExpr *object;
            KestExpr *index;
        } index;
        struct {
            KestExpr **items;
            uint32_t count;
        } array;
        struct {
            KestTextPart *parts;
            uint32_t count;
        } text;
    };
};

typedef struct KestStmt KestStmt;

typedef struct {
    KestStmt **items;
    uint32_t count;
} KestBlock;

typedef enum {
    KEST_STMT_LET,
    KEST_STMT_ASSIGN,
    KEST_STMT_EXPR,
    KEST_STMT_IF,
    KEST_STMT_WHILE,
    KEST_STMT_FOR,
    KEST_STMT_RETURN,
    KEST_STMT_BREAK,
    KEST_STMT_CONTINUE,
    KEST_STMT_BLOCK,
} KestStmtKind;

struct KestStmt {
    KestStmtKind kind;
    KestSpan span;
    union {
        struct {
            KestSpan name;
            // NULL when the type is left to inference, which is the usual case
            // inside a body; see D005.
            KestTypeRef *type;
            KestExpr *value;
        } let;
        struct {
            // KEST_TOK_EQ, or one of the compound assignment operators.
            KestTokenKind op;
            KestExpr *target;
            KestExpr *value;
        } assign;
        struct {
            // `if let x = maybe {`. Zero length for a plain `if`, and then the
            // condition is a `bool` rather than an optional.
            KestSpan binding;
            KestExpr *condition;
            KestBlock then_body;
            // KEST_STMT_BLOCK for `else`, KEST_STMT_IF for `else if`, NULL for
            // neither.
            KestStmt *otherwise;
        } branch;
        struct {
            KestExpr *condition;
            KestBlock body;
        } loop;
        struct {
            // `for i, x in a`. Zero length when the position was not asked
            // for, which is most of the time.
            KestSpan index;
            KestSpan name;
            KestExpr *sequence;
            KestBlock body;
        } each;
        // NULL for a bare `return`.
        KestExpr *result;
        KestExpr *value;
        KestBlock block;
    };
};

// A name and a type: a struct field, or a function parameter.
typedef struct {
    KestSpan name;
    KestTypeRef *type;
} KestField;

typedef enum {
    KEST_DECL_MODULE,
    KEST_DECL_IMPORT,
    KEST_DECL_CONST,
    KEST_DECL_STRUCT,
    KEST_DECL_FN,
} KestDeclKind;

typedef struct {
    KestDeclKind kind;
    KestSpan span;
    // The declared name. For a module or an import it covers the whole dotted
    // path.
    KestSpan name;
    union {
        struct {
            KestTypeRef *type;
            KestExpr *value;
        } constant;
        struct {
            KestField **fields;
            uint32_t field_count;
        } record;
        struct {
            // `Clock` in `extern fn Clock.now()`. Zero length when absent.
            KestSpan receiver;
            KestField **params;
            uint32_t param_count;
            // NULL when the function returns nothing.
            KestTypeRef *result;
            bool is_extern;
            bool no_alloc;
            KestBlock body;
        } function;
    };
} KestDecl;

typedef struct {
    KestDecl **items;
    uint32_t count;
} KestUnit;

// Prints the tree as indented s-expressions, for seeing what the parser built.
void kest_ast_dump(const KestUnit *unit, const KestSource *source, FILE *out);

#endif
