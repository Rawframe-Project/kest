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
    KEST_TYPE_FN,       // fn(i32, i32) -> bool no.alloc
} KestTypeKind;

typedef struct KestTypeRef KestTypeRef;

struct KestTypeRef {
    KestTypeKind kind;
    KestSpan span;
    // NAMED and GENERIC only.
    KestSpan name;
    KestTypeRef **args;
    uint32_t arg_count;
    // ARRAY, OPTIONAL and FN only. NULL for a function that gives nothing.
    KestTypeRef *element;
    // `[f32; 16]`. Zero is `[f32]`, which is a handle to something that can
    // grow; a count makes it that many, laid out where it stands.
    KestSpan count;
    // FN only. What the value promises, which is part of what it is. Two of
    // them, and a value promising more may go where one promising less is
    // wanted. See D853.
    bool no_alloc;
    bool no_host;
};

typedef enum {
    KEST_EXPR_INT,
    KEST_EXPR_FLOAT,
    KEST_EXPR_STRING,
    KEST_EXPR_BYTE,
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
    KEST_EXPR_MATCH,
    KEST_EXPR_IF,
} KestExprKind;

typedef struct KestExpr KestExpr;
typedef struct KestArm KestArm;
typedef struct KestBranch KestBranch;

// What a `match` is, whichever it is used as.
typedef struct {
    // One or more. Two enums answered together is one `match` rather than one
    // inside another, so an arm answers a case for each of them.
    KestExpr **subjects;
    uint32_t subject_count;
    KestArm *arms;
    uint32_t arm_count;
    // Set by the checker when every case is answered.
    bool total;
    // Set when the arms give values, which is when every one of them does.
    bool gives;
} KestChoose;

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
    // Set by the checker where a plain value stands in a place that wants an
    // optional. The compiler then writes the tag beside it. Nothing else in
    // the language converts on its own.
    //
    // Beside the kind rather than after the type, because a `bool` after two
    // words is seven bytes of padding on every node a program has. See D642.
    bool wrapped;
    KestSpan span;
    KestType *type;
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
        // Held through a pointer the way a branch beside it is. Written out
        // it is the widest thing this union can be, and eight `match`
        // expressions in the whole of this tree were making every name and
        // every `+` eight bytes wider. See D782.
        KestChoose *choose;
        // Out of line because it holds blocks, which are named below this.
        KestBranch *branch;
    };
};

typedef struct KestStmt KestStmt;

typedef struct {
    KestStmt **items;
    uint32_t count;
} KestBlock;

// What an `if` is, whichever it is used as. An arm gives a value when it is
// written `-> expression` and does something when it is a block, and both arms
// are the same kind, which is D027's rule and not a second one.
struct KestBranch {
    // `if let x = maybe`. Zero length for a plain `if`.
    KestSpan binding;
    KestExpr *condition;
    KestExpr *then_value;
    KestBlock then_body;
    KestExpr *else_value;
    KestBlock else_body;
    // An `else if`, which is another `if`.
    KestExpr *otherwise;
    bool has_else;
    bool gives;
};


// One arm of a match: the case it is for, the names it gives what that case
// carries, and what to do. A zero-length name is the `else` arm.
//
// An arm either gives a value, written `-> expression`, or does something,
// written as a block. Every arm of one match is the same kind, which is what
// makes a match either a value or a statement and never quietly both.
// One position of an arm: the case answered there, and the names given to
// whatever that case carries. A zero length name is `else`, which answers any
// case in that position.
typedef struct KestArmPart {
    KestSpan name;
    KestSpan *bindings;
    uint32_t binding_count;
} KestArmPart;

struct KestArm {
    // One per subject, or one `else` standing for all of them.
    KestArmPart *parts;
    uint32_t part_count;
    KestSpan span;
    KestExpr *value;
    KestBlock body;
};

typedef enum {
    KEST_STMT_LET,
    KEST_STMT_ASSIGN,
    KEST_STMT_EXPR,
    KEST_STMT_WHILE,
    KEST_STMT_FOR,
    KEST_STMT_RETURN,
    KEST_STMT_BREAK,
    KEST_STMT_CONTINUE,
    KEST_STMT_BLOCK,
    KEST_STMT_DEFER,
} KestStmtKind;

// What a `for` walks. `index` is zero length when the position was not asked
// for, which is most of the time; `until` non-NULL makes `sequence` the first
// number of a range rather than the thing being walked. Named so a statement
// can hold one through a pointer rather than inside itself. See D782.
typedef struct {
    KestSpan index;
    KestSpan name;
    KestExpr *sequence;
    KestExpr *until;
    KestBlock body;
} KestEach;

struct KestStmt {
    KestStmtKind kind;
    // Set by the checker on the last statement of an arm written as a block
    // where a value was meant, so the one message about the arms is not said
    // again once for every arm. See D516.
    //
    // Beside the kind for the reason the same bit is beside an expression's:
    // a `bool` after a span is padding on every statement a program has. See
    // D642.
    bool passed_over;
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
            // `while let one = next()`. Zero length for a plain `while`, and
            // then the condition is a `bool` rather than an optional.
            KestSpan binding;
            KestExpr *condition;
            KestBlock body;
        } loop;
        // Held through a pointer, the way a `match` is. Written out it is the
        // widest thing this union can be by sixteen bytes, and a `for` is one
        // statement in twenty-five: the other twenty-four were carrying the
        // room for a position, a name, a sequence, an until and a body, and
        // using none of it. See D782.
        KestEach *each;
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

// One case of an enum: its name and what it carries, by position.
typedef struct {
    KestSpan name;
    KestTypeRef **payload;
    uint32_t payload_count;
} KestVariant;

typedef enum {
    KEST_DECL_MODULE,
    KEST_DECL_IMPORT,
    KEST_DECL_CONST,
    KEST_DECL_STRUCT,
    KEST_DECL_ENUM,
    KEST_DECL_FLAGS,
    KEST_DECL_FN,
} KestDeclKind;

typedef struct {
    KestDeclKind kind;
    // Beside the kind, because two four byte numbers together are eight bytes
    // and apart they are sixteen. See D642.
    uint32_t type_param_count;
    KestSpan span;
    // The declared name. For a module or an import it covers the whole dotted
    // path.
    KestSpan name;
    // `fn sort<T>(...)` and `struct Pair<A, B>`. A copy is made per set of
    // types it is used with, so a name here stands for one type per copy.
    KestSpan *type_params;
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
            KestVariant **cases;
            uint32_t case_count;
            // `flags State: u8`. The width is written rather than counted,
            // because it is what a host sees and a ninth flag must not change
            // it quietly.
            KestTypeRef *width;
        } choice;
        struct {
            // `Clock` in `extern fn Clock.now()`. Zero length when absent.
            KestSpan receiver;
            KestField **params;
            uint32_t param_count;
            // NULL when the function returns nothing.
            KestTypeRef *result;
            bool is_extern;
            bool no_alloc;
            bool no_host;
            KestBlock body;
        } function;
    };
} KestDecl;

typedef struct {
    KestDecl **items;
    uint32_t count;
    // How many nodes are under those declarations: every expression, every
    // statement and every declaration the parser made for this file. A tree is
    // the biggest thing reading a file makes — more than the tokens it came
    // from — and what it is made of was a thing nobody could ask. Counted where
    // the nodes are made, because a walk to count them would be a second walk
    // of the one thing this file already walks. See D641.
    uint32_t nodes;
} KestUnit;

// Prints the tree as indented s-expressions, for seeing what the parser built.
void kest_ast_dump(const KestUnit *unit, const KestSource *source, FILE *out);

#endif
