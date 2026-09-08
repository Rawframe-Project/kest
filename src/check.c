#include "check.h"

#include <stdarg.h>
#include <string.h>

// A name visible at a point in a body. Depth is the block nesting it was
// declared at, so a scope can be dropped by rewinding the count.
typedef struct {
    const char *name;
    KestType *type;
    KestSpan span;
    uint32_t depth;
    // `for x in a` binds a copy of each element, and `for i, x` binds a copy
    // of the position. Assigning to either is legal and changes nothing that
    // outlives the turn, which is worth saying out loud.
    bool is_loop_element;
    bool is_loop_index;
} Local;

typedef struct {
    KestProgram *program;
    Local *locals;
    uint32_t local_count;
    uint32_t local_capacity;
    uint32_t depth;
    // The current function's declared result, so `return` has something to be
    // measured against.
    KestType *result;
    uint32_t loop_depth;
    // Set while the operand of a unary minus is being checked, so `-128` is
    // read as one number rather than as the negation of one that does not fit.
    bool negating;
    bool out_of_memory;
} Checker;

static KestType *check_expr(Checker *checker, KestExpr *expr,
                            const KestType *expected);

static const char *type_name(Checker *checker, const KestType *type) {
    return kest_type_name(checker->program->arena, type);
}

static KestType *builtin(Checker *checker, const char *name) {
    return kest_find_type(checker->program, name, strlen(name));
}

static KestType *error_type(Checker *checker) {
    KestType *type = KEST_ARENA_NEW(checker->program->arena, KestType);
    if (type == NULL) {
        checker->out_of_memory = true;
        return NULL;
    }
    type->tag = KEST_T_ERROR;
    return type;
}

static const char *span_text(Checker *checker, KestSpan span) {
    return checker->program->source->text + span.offset;
}

static void report(Checker *checker, KestSpan span, const char *code,
                   const char *format, ...) {
    va_list args;
    va_start(args, format);
    char message[512];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    kest_diags_add(checker->program->diags, KEST_SEVERITY_ERROR, code, span,
                   "%s", message);
}

static void suggest(Checker *checker, const char *format, ...) {
    va_list args;
    va_start(args, format);
    char message[512];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    kest_diags_suggest(checker->program->diags, "%s", message);
}

// Reports a mismatch in the one shape every mismatch is reported in, so a
// reader learns to read it once.
static void expected_but(Checker *checker, KestSpan span, const KestType *want,
                         const KestType *got, const char *where) {
    report(checker, span, "K0310", "%s expects `%s`, found `%s`", where,
           type_name(checker, want), type_name(checker, got));
}

static Local *find_local(Checker *checker, const char *name, size_t length) {
    // Backwards, so the innermost declaration of a name is the one found.
    for (uint32_t i = checker->local_count; i > 0; i--) {
        Local *local = &checker->locals[i - 1];
        if (strlen(local->name) == length &&
            memcmp(local->name, name, length) == 0) {
            return local;
        }
    }
    return NULL;
}

static void declare_local(Checker *checker, KestSpan span, KestType *type) {
    const char *name = kest_arena_strndup(
        checker->program->arena, span_text(checker, span), span.length);
    if (name == NULL) {
        checker->out_of_memory = true;
        return;
    }

    Local *existing = find_local(checker, name, span.length);
    if (existing != NULL) {
        // Shadowing a visible local is refused: at any point in a body one
        // name means one thing. A sibling scope may reuse the name, because
        // the first is gone by then.
        report(checker, span, "K0318", "`%s` is already declared here", name);
        kest_diags_note(checker->program->diags, NULL, existing->span,
                        "the first one");
        return;
    }

    if (checker->local_count == checker->local_capacity) {
        uint32_t grown =
            checker->local_capacity == 0 ? 16 : checker->local_capacity * 2;
        Local *moved = KEST_ARENA_ARRAY(checker->program->arena, Local, grown);
        if (moved == NULL) {
            checker->out_of_memory = true;
            return;
        }
        if (checker->local_count > 0) {
            memcpy(moved, checker->locals,
                   sizeof(Local) * checker->local_count);
        }
        checker->locals = moved;
        checker->local_capacity = grown;
    }

    Local *local = &checker->locals[checker->local_count++];
    local->name = name;
    local->type = type;
    local->span = span;
    local->depth = checker->depth;
    local->is_loop_element = false;
    local->is_loop_index = false;
}

// Whether writing through this path can be seen after the statement. An array
// anywhere along it is a handle, and writing through a handle is visible
// however the path reached it.
static bool writes_only_a_copy(Checker *checker, const KestExpr *target,
                               const KestExpr **root, bool *is_index) {
    const KestExpr *step = target;
    while (step->kind == KEST_EXPR_FIELD || step->kind == KEST_EXPR_INDEX) {
        if (step->kind == KEST_EXPR_INDEX) {
            return false;
        }
        step = step->field.object;
    }
    if (step->kind != KEST_EXPR_NAME) {
        return false;
    }
    Local *local =
        find_local(checker, span_text(checker, step->span), step->span.length);
    *root = step;
    if (local == NULL || !local->is_loop_element) {
        return false;
    }
    *is_index = local->is_loop_index;
    return true;
}

// An optional is a place a value can go, not a hint about the value itself,
// so an operand is measured against what the optional holds.
static const KestType *inside(const KestType *expected) {
    if (expected != NULL && expected->tag == KEST_T_OPTIONAL) {
        return expected->element;
    }
    return expected;
}

static bool is_numeric(const KestType *type) {
    return type != NULL &&
           (type->tag == KEST_T_INT || type->tag == KEST_T_FLOAT);
}

static bool is_error(const KestType *type) {
    return type == NULL || type->tag == KEST_T_ERROR;
}

// Whether a literal can take a type from its context rather than its default.
// `let x: f64 = 1.5` and `let n: u8 = 200` both work because of this, and
// nothing else in the language converts silently.
static bool is_literal(const KestExpr *expr) {
    if (expr->kind == KEST_EXPR_INT || expr->kind == KEST_EXPR_FLOAT) {
        return true;
    }
    return expr->kind == KEST_EXPR_UNARY &&
           expr->unary.op == KEST_TOK_MINUS && is_literal(expr->unary.operand);
}

static KestType *check_name(Checker *checker, KestExpr *expr) {
    const char *name = span_text(checker, expr->span);
    size_t length = expr->span.length;

    Local *local = find_local(checker, name, length);
    if (local != NULL) {
        return local->type;
    }

    KestSymbol *global = kest_lookup_global(checker->program, name, length);
    if (global != NULL) {
        return global->type;
    }

    report(checker, expr->span, "K0306", "unknown name `%.*s`", (int)length,
           name);
    // Saying something is the host's to do, and it is the first thing anybody
    // reaches for, so the one place it lives is worth naming outright.
    if ((length == 5 && memcmp(name, "print", 5) == 0) ||
        (length == 5 && memcmp(name, "write", 5) == 0)) {
        suggest(checker, "`import std.io` and call `io.%.*s`", (int)length,
                name);
        return error_type(checker);
    }
    const char *nearest = kest_nearest_global(checker->program, name, length);
    if (nearest != NULL) {
        suggest(checker, "did you mean `%s`?", nearest);
    }
    return error_type(checker);
}

// `Vec3(1.0, 2.0, 3.0)` builds a struct. Call syntax rather than a braced
// literal, because `if p.y < 0 {` only parses without a rule about where a
// brace may start an expression, and there is no rule to write if no
// expression ever begins with one.
static void report_unimported(Checker *checker, KestSpan name) {
    const char *text = span_text(checker, name);
    if (!kest_needs_import(checker->program, text, name.length)) {
        return;
    }
    const char *dot = memchr(text, '.', name.length);
    report(checker, name, "K0325", "this file does not import `%.*s`",
           (int)(dot - text), text);
    suggest(checker, "a name is only reachable from a module this file asked "
                     "for");

    // Where it came from, which is the thing the reader has to go and look at.
    KestType *type = kest_lookup_type(checker->program, text, name.length);
    if (type != NULL && type->declared_in != NULL) {
        kest_diags_note(checker->program->diags, type->declared_in, type->span,
                        "declared here");
        return;
    }
    KestSymbol *symbol = kest_lookup_global(checker->program, text, name.length);
    if (symbol != NULL && symbol->source != NULL) {
        kest_diags_note(checker->program->diags, symbol->source, symbol->span,
                        "declared here");
    }
}

// One case of an enum, found by name, or nothing with a diagnostic that lists
// what the enum does have.
static const KestVariantType *find_case(Checker *checker, const KestType *choice,
                                        KestSpan name) {
    const char *text = span_text(checker, name);
    for (uint32_t i = 0; i < choice->case_count; i++) {
        if (strlen(choice->cases[i].name) == name.length &&
            memcmp(choice->cases[i].name, text, name.length) == 0) {
            return &choice->cases[i];
        }
    }
    report(checker, name, "K0330", "`%s` has no case `%.*s`", choice->name,
           (int)name.length, text);
    for (uint32_t i = 0; i < choice->case_count; i++) {
        kest_diags_note(checker->program->diags, choice->declared_in,
                        choice->cases[i].span, "this one it has");
    }
    return NULL;
}

static KestType *check_case(Checker *checker, KestExpr *expr, KestType *choice,
                            KestSpan name) {
    expr->call.callee->type = choice;
    const KestVariantType *variant = find_case(checker, choice, name);
    if (variant == NULL) {
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            check_expr(checker, expr->call.args[i], NULL);
        }
        return error_type(checker);
    }

    if (expr->call.arg_count != variant->payload_count) {
        report(checker, expr->span, "K0309",
               "`%s` carries %u thing%s, found %u", variant->name,
               variant->payload_count, variant->payload_count == 1 ? "" : "s",
               expr->call.arg_count);
    }
    uint32_t checked = expr->call.arg_count < variant->payload_count
                           ? expr->call.arg_count
                           : variant->payload_count;
    for (uint32_t i = 0; i < checked; i++) {
        KestType *given =
            check_expr(checker, expr->call.args[i], variant->payload[i]);
        if (!kest_type_equal(given, variant->payload[i])) {
            expected_but(checker, expr->call.args[i]->span, variant->payload[i],
                         given, "this one");
        }
    }
    for (uint32_t i = checked; i < expr->call.arg_count; i++) {
        check_expr(checker, expr->call.args[i], NULL);
    }
    return choice;
}

static KestType *check_construction(Checker *checker, KestExpr *expr,
                                    KestType *type) {
    expr->call.callee->type = type;

    if (expr->call.arg_count != type->member_count) {
        report(checker, expr->span, "K0309",
               "`%s` has %u field%s, found %u", type->name, type->member_count,
               type->member_count == 1 ? "" : "s", expr->call.arg_count);
    }

    uint32_t checked = expr->call.arg_count < type->member_count
                           ? expr->call.arg_count
                           : type->member_count;
    for (uint32_t i = 0; i < checked; i++) {
        KestType *argument =
            check_expr(checker, expr->call.args[i], type->members[i].type);
        if (!kest_type_equal(argument, type->members[i].type)) {
            expected_but(checker, expr->call.args[i]->span,
                         type->members[i].type, argument, "this field");
        }
    }
    for (uint32_t i = checked; i < expr->call.arg_count; i++) {
        check_expr(checker, expr->call.args[i], NULL);
    }
    return type;
}

// The builtins are checked here rather than declared, because nothing in the
// type system can yet say "an array of anything" or "whatever this store
// holds". A file that declares its own function of the same name gets that
// one, so none of these is a reserved word.
static bool is_builtin(Checker *checker, KestSpan name, const char *word) {
    size_t length = strlen(word);
    return name.length == length &&
           memcmp(span_text(checker, name), word, length) == 0 &&
           kest_lookup_global(checker->program, word, length) == NULL;
}

static uint32_t check_arity(Checker *checker, KestExpr *expr, uint32_t want) {
    if (expr->call.arg_count != want) {
        report(checker, expr->span, "K0309", "expected %u argument%s, found %u",
               want, want == 1 ? "" : "s", expr->call.arg_count);
    }
    return expr->call.arg_count < want ? expr->call.arg_count : want;
}

// The store and what it holds, or NULL when the first argument is not one.
static KestType *check_store_argument(Checker *checker, KestExpr *expr,
                                      const char *word) {
    KestType *store = check_expr(checker, expr->call.args[0], NULL);
    if (is_error(store)) {
        return NULL;
    }
    if (store->tag != KEST_T_STORE) {
        report(checker, expr->call.args[0]->span, "K0310",
               "`%s` works on a store, found `%s`", word,
               type_name(checker, store));
        return NULL;
    }
    return store;
}

static void check_ref_argument(Checker *checker, KestExpr *expr, uint32_t at,
                               const KestType *store) {
    KestType *wanted = kest_ref_of(checker->program, store->element);
    KestType *handle = check_expr(checker, expr->call.args[at], wanted);
    if (!kest_type_equal(handle, wanted)) {
        expected_but(checker, expr->call.args[at]->span, wanted, handle,
                     "this reference");
    }
}

static KestType *check_builtin(Checker *checker, KestExpr *expr,
                               const KestType *expected, bool *handled) {
    *handled = true;
    KestSpan name = expr->call.callee->span;

    if (is_builtin(checker, name, "store")) {
        check_arity(checker, expr, 0);
        if (expected == NULL || expected->tag != KEST_T_STORE) {
            report(checker, expr->span, "K0322", "`store()` has no type here");
            kest_diags_suggest(checker->program->diags,
                               "write what it holds: "
                               "`let w: store<Npc> = store()`");
            return error_type(checker);
        }
        return (KestType *)expected;
    }

    if (is_builtin(checker, name, "array")) {
        // An empty one takes what it holds from where it is going, the same
        // way `store()` does, because there is nothing to read it off.
        if (expr->call.arg_count == 0) {
            if (expected == NULL || expected->tag != KEST_T_ARRAY) {
                report(checker, expr->span, "K0335",
                       "`array()` has no type here");
                kest_diags_suggest(checker->program->diags,
                                   "write what it holds: "
                                   "`let bytes: [u8] = array()`");
                return error_type(checker);
            }
            return (KestType *)expected;
        }
        if (check_arity(checker, expr, 2) < 2) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return error_type(checker);
        }
        KestType *count = check_expr(checker, expr->call.args[0],
                                     builtin(checker, "i32"));
        if (!is_error(count) && count->tag != KEST_T_INT) {
            report(checker, expr->call.args[0]->span, "K0310",
                   "a count is an integer, found `%s`",
                   type_name(checker, count));
        }
        // What it holds comes from what it is filled with, so nothing has to
        // be written down twice.
        KestType *element = check_expr(checker, expr->call.args[1], NULL);
        return kest_array_of(checker->program, element);
    }

    if (is_builtin(checker, name, "push")) {
        if (check_arity(checker, expr, 2) < 2) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return builtin(checker, "void");
        }
        KestType *array = check_expr(checker, expr->call.args[0], NULL);
        if (is_error(array) || array->tag != KEST_T_ARRAY) {
            if (!is_error(array)) {
                report(checker, expr->call.args[0]->span, "K0310",
                       "`push` puts something on an array, found `%s`",
                       type_name(checker, array));
            }
            check_expr(checker, expr->call.args[1], NULL);
            return builtin(checker, "void");
        }
        KestType *value =
            check_expr(checker, expr->call.args[1], array->element);
        if (!kest_type_equal(value, array->element)) {
            expected_but(checker, expr->call.args[1]->span, array->element,
                         value, "this value");
        }
        return builtin(checker, "void");
    }

    // Taking things out of an array. A store answers this with `remove` and a
    // reference; here a position means something, so what is after what went
    // keeps its order and the cost of that is on `remove` where it is written.
    if (is_builtin(checker, name, "pop") || is_builtin(checker, name, "remove") ||
        is_builtin(checker, name, "clear")) {
        bool taking = is_builtin(checker, name, "remove");
        bool emptying = is_builtin(checker, name, "clear");
        uint32_t wanted = taking ? 2 : 1;
        if (check_arity(checker, expr, wanted) < wanted) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return emptying ? builtin(checker, "void") : error_type(checker);
        }
        KestType *array = check_expr(checker, expr->call.args[0], NULL);
        // A store answers `remove` too, and with a reference rather than a
        // position, so which one is meant is settled by what is handed in.
        if (taking && !is_error(array) && array->tag == KEST_T_STORE) {
            check_ref_argument(checker, expr, 1, array);
            return builtin(checker, "bool");
        }
        for (uint32_t i = 1; i < expr->call.arg_count; i++) {
            const KestType *want = builtin(checker, "i32");
            KestType *given = check_expr(checker, expr->call.args[i], want);
            if (i < wanted && !kest_type_equal(given, want)) {
                expected_but(checker, expr->call.args[i]->span, want, given,
                             "this position");
            }
        }
        if (is_error(array) || array->tag != KEST_T_ARRAY) {
            if (!is_error(array)) {
                report(checker, expr->call.args[0]->span, "K0310",
                       "`%s` works on an array%s, found `%s`",
                       emptying ? "clear" : (taking ? "remove" : "pop"),
                       taking ? " or a store" : "", type_name(checker, array));
            }
            return emptying ? builtin(checker, "void") : error_type(checker);
        }
        if (emptying) {
            return builtin(checker, "void");
        }
        // Taking a named position out is a position that is there; taking the
        // end off an array that may be empty is a lookup like any other.
        return taking ? array->element
                      : kest_optional_of(checker->program, array->element);
    }

    if (is_builtin(checker, name, "slice") || is_builtin(checker, name, "find")) {
        bool slicing = is_builtin(checker, name, "slice");
        uint32_t wanted = slicing ? 3 : 2;
        if (check_arity(checker, expr, wanted) < wanted) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return error_type(checker);
        }

        KestType *subject =
            check_expr(checker, expr->call.args[0], builtin(checker, "text"));
        if (!is_error(subject) && subject->tag != KEST_T_TEXT) {
            report(checker, expr->call.args[0]->span, "K0310",
                   "`%s` works on text, found `%s`", slicing ? "slice" : "find",
                   type_name(checker, subject));
        }

        for (uint32_t i = 1; i < wanted; i++) {
            const KestType *want =
                slicing ? builtin(checker, "i32") : builtin(checker, "text");
            KestType *given = check_expr(checker, expr->call.args[i], want);
            if (!kest_type_equal(given, want)) {
                expected_but(checker, expr->call.args[i]->span, want, given,
                             "this argument");
            }
        }
        for (uint32_t i = wanted; i < expr->call.arg_count; i++) {
            check_expr(checker, expr->call.args[i], NULL);
        }

        // Finding something that is not there is a lookup like any other.
        return slicing ? builtin(checker, "text")
                       : kest_optional_of(checker->program,
                                          builtin(checker, "i32"));
    }

    if (is_builtin(checker, name, "len")) {
        uint32_t checked = check_arity(checker, expr, 1);
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            KestType *argument = check_expr(checker, expr->call.args[i], NULL);
            if (i == 0 && checked > 0 && !is_error(argument) &&
                argument->tag != KEST_T_ARRAY &&
                argument->tag != KEST_T_STORE &&
                argument->tag != KEST_T_TEXT) {
                report(checker, expr->call.args[i]->span, "K0310",
                       "`len` counts an array, a store or text, found `%s`",
                       type_name(checker, argument));
            }
        }
        return builtin(checker, "i32");
    }

    if (is_builtin(checker, name, "add")) {
        if (check_arity(checker, expr, 2) < 2) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return error_type(checker);
        }
        KestType *store = check_store_argument(checker, expr, "add");
        if (store == NULL) {
            check_expr(checker, expr->call.args[1], NULL);
            return error_type(checker);
        }
        KestType *value = check_expr(checker, expr->call.args[1], store->element);
        if (!kest_type_equal(value, store->element)) {
            expected_but(checker, expr->call.args[1]->span, store->element,
                         value, "this value");
        }
        return kest_ref_of(checker->program, store->element);
    }

    if (is_builtin(checker, name, "get")) {
        bool getting = true;
        if (check_arity(checker, expr, 2) < 2) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return error_type(checker);
        }
        KestType *store =
            check_store_argument(checker, expr, getting ? "get" : "remove");
        if (store == NULL) {
            check_expr(checker, expr->call.args[1], NULL);
            return error_type(checker);
        }
        check_ref_argument(checker, expr, 1, store);
        // Reading through a reference is a lookup that can fail, so what comes
        // back is an optional and D013 is what opens it.
        return getting ? kest_optional_of(checker->program, store->element)
                       : builtin(checker, "bool");
    }

    if (is_builtin(checker, name, "set")) {
        if (check_arity(checker, expr, 3) < 3) {
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return error_type(checker);
        }
        KestType *store = check_store_argument(checker, expr, "set");
        if (store == NULL) {
            check_expr(checker, expr->call.args[1], NULL);
            check_expr(checker, expr->call.args[2], NULL);
            return error_type(checker);
        }
        check_ref_argument(checker, expr, 1, store);
        KestType *value = check_expr(checker, expr->call.args[2], store->element);
        if (!kest_type_equal(value, store->element)) {
            expected_but(checker, expr->call.args[2]->span, store->element,
                         value, "this value");
        }
        return builtin(checker, "bool");
    }

    *handled = false;
    return NULL;
}

// Every function the callee could name. A dotted callee is one name with a dot
// in it, so both shapes are looked up the same way.
static uint32_t find_callable(Checker *checker, const KestExpr *expr,
                              KestSymbol **found, uint32_t room) {
    const KestExpr *callee = expr->call.callee;
    if (callee->kind != KEST_EXPR_NAME &&
        !(callee->kind == KEST_EXPR_FIELD &&
          callee->field.object->kind == KEST_EXPR_NAME)) {
        return 0;
    }

    const char *text = span_text(checker, callee->span);
    size_t length = callee->span.length;
    const char *alias = checker->program->alias;
    if (alias[0] != '\0') {
        char joined[256];
        int written = snprintf(joined, sizeof(joined), "%s.%.*s", alias,
                               (int)length, text);
        if (written > 0 && (size_t)written < sizeof(joined)) {
            uint32_t count = kest_overloads(checker->program, joined,
                                            (size_t)written, found, room);
            if (count > 0) {
                return count;
            }
        }
    }
    return kest_overloads(checker->program, text, length, found, room);
}

// A literal has no type of its own to lose, so it is the one thing that may
// take a type from the function that was chosen rather than the other way
// round.
static bool takes_a_type(const KestExpr *expr) {
    if (expr->kind == KEST_EXPR_INT || expr->kind == KEST_EXPR_FLOAT) {
        return true;
    }
    return expr->kind == KEST_EXPR_UNARY &&
           expr->unary.op == KEST_TOK_MINUS && takes_a_type(expr->unary.operand);
}

static KestType *check_arguments(Checker *checker, KestExpr *expr,
                                 const KestType *callee);

// What a literal is, before anything has told it otherwise.
static const KestExpr *literal_of(const KestExpr *expr) {
    if (expr->kind == KEST_EXPR_INT || expr->kind == KEST_EXPR_FLOAT) {
        return expr;
    }
    if (expr->kind == KEST_EXPR_UNARY && expr->unary.op == KEST_TOK_MINUS) {
        return literal_of(expr->unary.operand);
    }
    return NULL;
}

static bool literal_suits(Checker *checker, const KestExpr *expr,
                          const KestType *want, bool exactly) {
    const KestExpr *literal = literal_of(expr);
    if (want == NULL) {
        return false;
    }
    bool integer = literal->kind == KEST_EXPR_INT;
    if (!exactly) {
        // Any width of the right family will take it.
        return want->tag == (integer ? KEST_T_INT : KEST_T_FLOAT);
    }
    return kest_type_equal((KestType *)want,
                           builtin(checker, integer ? "i32" : "f32"));
}

static KestType *check_overloaded(Checker *checker, KestExpr *expr,
                                  KestSymbol **candidates, uint32_t count) {
    // What each argument is, found out rather than reported, because the
    // wrong choice would report against the wrong function.
    KestDiags *diags = checker->program->diags;
    kest_diags_mute(diags, true);
    KestType *given[16];
    uint32_t argument_count = expr->call.arg_count < 16 ? expr->call.arg_count
                                                        : 16;
    for (uint32_t i = 0; i < argument_count; i++) {
        given[i] = check_expr(checker, expr->call.args[i], NULL);
    }
    kest_diags_mute(diags, false);

    // Once by family, so a literal fits any width of the right kind, and
    // again exactly, for when the literals are all there is to go on.
    KestSymbol *chosen = NULL;
    uint32_t matches = 0;
    for (uint32_t pass = 0; pass < 2 && matches != 1; pass++) {
        chosen = NULL;
        matches = 0;
        for (uint32_t c = 0; c < count; c++) {
            const KestType *type = candidates[c]->type;
            if (type->param_count != expr->call.arg_count ||
                expr->call.arg_count > 16) {
                continue;
            }
            bool fits = true;
            for (uint32_t i = 0; i < argument_count && fits; i++) {
                if (takes_a_type(expr->call.args[i])) {
                    fits = literal_suits(checker, expr->call.args[i],
                                         type->params[i], pass == 1);
                } else {
                    fits = kest_type_equal(given[i], type->params[i]);
                }
            }
            if (fits) {
                chosen = candidates[c];
                matches++;
            }
        }
    }

    if (matches != 1) {
        report(checker, expr->span, "K0329",
               matches == 0 ? "no `%.*s` takes these"
                            : "more than one `%.*s` takes these",
               (int)expr->call.callee->span.length,
               span_text(checker, expr->call.callee->span));
        for (uint32_t c = 0; c < count; c++) {
            char shape[256];
            int used = 0;
            for (uint32_t p = 0; p < candidates[c]->type->param_count &&
                                 used >= 0 && (size_t)used < sizeof(shape);
                 p++) {
                used += snprintf(shape + used, sizeof(shape) - (size_t)used,
                                 "%s%s", p == 0 ? "" : ", ",
                                 kest_type_name(checker->program->arena,
                                                candidates[c]->type->params[p]));
            }
            shape[used < 0 ? 0 : used] = '\0';
            kest_diags_note(diags, candidates[c]->source, candidates[c]->span,
                            "this one takes (%s)", shape);
        }
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            check_expr(checker, expr->call.args[i], NULL);
        }
        return error_type(checker);
    }

    expr->call.callee->type = chosen->type;
    return check_arguments(checker, expr, chosen->type);
}

static KestType *check_call(Checker *checker, KestExpr *expr,
                            const KestType *expected) {
    if (expr->call.callee->kind == KEST_EXPR_NAME) {
        bool handled = false;
        KestType *result = check_builtin(checker, expr, expected, &handled);
        if (handled) {
            return result;
        }
    }

    // A case of an enum is built by naming it after its enum, which is one
    // name with a dot in it like everything else that has one.
    if (expr->call.callee->kind == KEST_EXPR_FIELD &&
        expr->call.callee->field.object->kind == KEST_EXPR_NAME) {
        KestSpan owner = expr->call.callee->field.object->span;
        KestType *choice = kest_lookup_type(checker->program,
                                            span_text(checker, owner),
                                            owner.length);
        if (choice != NULL && choice->tag == KEST_T_ENUM) {
            report_unimported(checker, owner);
            return check_case(checker, expr, choice,
                              expr->call.callee->field.name);
        }
    }

    // A struct is built by naming it, and a struct from another module is
    // named with a dot, which is one name and not a field of anything.
    if (expr->call.callee->kind == KEST_EXPR_NAME ||
        (expr->call.callee->kind == KEST_EXPR_FIELD &&
         expr->call.callee->field.object->kind == KEST_EXPR_NAME)) {
        KestSpan name = expr->call.callee->span;
        KestType *type = kest_lookup_type(checker->program,
                                          span_text(checker, name), name.length);
        if (type != NULL && type->tag == KEST_T_STRUCT) {
            report_unimported(checker, name);
            return check_construction(checker, expr, type);
        }
        // A set of bits with none of them set, or one made out of a number
        // the host handed over. `array()` and `store()` already read "an
        // empty one" from a name with nothing in the brackets (D030).
        if (type != NULL && type->tag == KEST_T_FLAGS) {
            expr->call.callee->type = type;
            if (expr->call.arg_count > 1) {
                check_arity(checker, expr, 1);
            }
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                KestType *from = check_expr(checker, expr->call.args[i], NULL);
                if (i == 0 && !is_error(from) &&
                    (from->tag != KEST_T_INT || from->is_signed ||
                     from->width != type->width)) {
                    report(checker, expr->call.args[i]->span, "K0327",
                           "`%s` is made from a `u%u`, found `%s`", type->name,
                           type->width, type_name(checker, from));
                    kest_diags_suggest(checker->program->diags,
                                       "`%s()` is the empty one", type->name);
                }
            }
            return type;
        }
        // Naming a number type makes one, the same way naming a struct does.
        // Nothing converts on its own, so every one of these is written down.
        if (type != NULL &&
            (type->tag == KEST_T_INT || type->tag == KEST_T_FLOAT)) {
            expr->call.callee->type = type;
            check_arity(checker, expr, 1);
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                KestType *from = check_expr(checker, expr->call.args[i], NULL);
                // A flag set is bits over an integer, so a number of that
                // width is what it already is. A narrower one would lose
                // flags silently, which is what nothing here does.
                bool bits = !is_error(from) && from->tag == KEST_T_FLAGS &&
                            type->tag == KEST_T_INT && !type->is_signed &&
                            type->width == from->width;
                if (i == 0 && !bits && !is_error(from)) {
                    if (from->tag == KEST_T_FLAGS) {
                        report(checker, expr->call.args[i]->span, "K0327",
                               "`%s` is %u bits, and `%s` is not",
                               type_name(checker, from), from->width,
                               type->name);
                    } else if (from->tag != KEST_T_INT &&
                               from->tag != KEST_T_FLOAT &&
                               from->tag != KEST_T_BOOL) {
                        report(checker, expr->call.args[i]->span, "K0327",
                               "there is no `%s` for `%s`", type->name,
                               type_name(checker, from));
                    }
                }
            }
            return type;
        }
        // Text is its bytes (D021), so a run of them is the one thing it can
        // be made from. That is what lets text be built a piece at a time:
        // the pieces go on an array and become text once.
        if (type != NULL && type->tag == KEST_T_TEXT &&
            expr->call.arg_count == 1) {
            expr->call.callee->type = type;
            KestType *from = check_expr(checker, expr->call.args[0], NULL);
            if (!is_error(from) &&
                (from->tag != KEST_T_ARRAY || from->element == NULL ||
                 from->element->tag != KEST_T_INT ||
                 from->element->width != 8 || from->element->is_signed)) {
                report(checker, expr->call.args[0]->span, "K0327",
                       "text is made from `[u8]`, found `%s`",
                       type_name(checker, from));
                kest_diags_suggest(checker->program->diags,
                                   "a string with a hole in it makes one "
                                   "from a value: `\"{x}\"`");
            }
            return type;
        }
        // A type that is not a number and not a struct is not something a
        // value turns into, and saying so beats reporting the name as unknown.
        if (type != NULL && type->tag != KEST_T_ERROR) {
            report(checker, expr->span, "K0327",
                   "there is no way to make a `%s` from a value",
                   type_name(checker, type));
            if (type->tag == KEST_T_TEXT) {
                kest_diags_suggest(checker->program->diags,
                                   "a string with a hole in it does that: "
                                   "`\"{x}\"`");
            }
            for (uint32_t i = 0; i < expr->call.arg_count; i++) {
                check_expr(checker, expr->call.args[i], NULL);
            }
            return error_type(checker);
        }
    }

    // Which function is meant is settled by what is passed, and only when
    // there is more than one to choose between.
    KestSymbol *candidates[16];
    uint32_t candidate_count = find_callable(checker, expr, candidates, 16);
    if (candidate_count > 1) {
        return check_overloaded(checker, expr, candidates, candidate_count);
    }

    // `Clock.now()` is one name with a dot in it, not a field of a `Clock`.
    // An extern is declared against the host type it belongs to, so the
    // receiver is part of what it is called.
    KestType *callee = NULL;
    if (expr->call.callee->kind == KEST_EXPR_FIELD &&
        expr->call.callee->field.object->kind == KEST_EXPR_NAME) {
        KestSpan whole = expr->call.callee->span;
        // `world.spawn()` and `Clock.now()` are both one name with a dot in
        // it: a module qualifier and a host type read the same way.
        KestSymbol *host = kest_lookup_global(
            checker->program, span_text(checker, whole), whole.length);
        if (host != NULL && host->type->tag == KEST_T_FN) {
            report_unimported(checker, whole);
            expr->call.callee->type = host->type;
            callee = host->type;
        }
    }
    if (callee == NULL) {
        callee = check_expr(checker, expr->call.callee, NULL);
    }
    for (uint32_t i = 0; i < expr->call.arg_count; i++) {
        if (is_error(callee)) {
            check_expr(checker, expr->call.args[i], NULL);
        }
    }
    if (is_error(callee)) {
        return error_type(checker);
    }

    if (callee->tag != KEST_T_FN) {
        report(checker, expr->call.callee->span, "K0308",
               "`%s` is not a function", type_name(checker, callee));
        return error_type(checker);
    }

    return check_arguments(checker, expr, callee);
}

static KestType *check_arguments(Checker *checker, KestExpr *expr,
                                 const KestType *callee) {
    if (expr->call.arg_count != callee->param_count) {
        report(checker, expr->span, "K0309",
               "expected %u argument%s, found %u", callee->param_count,
               callee->param_count == 1 ? "" : "s", expr->call.arg_count);
    }

    uint32_t checked = expr->call.arg_count < callee->param_count
                           ? expr->call.arg_count
                           : callee->param_count;
    for (uint32_t i = 0; i < checked; i++) {
        KestType *argument =
            check_expr(checker, expr->call.args[i], callee->params[i]);
        if (!kest_type_equal(argument, callee->params[i])) {
            expected_but(checker, expr->call.args[i]->span, callee->params[i],
                         argument, "this argument");
        }
    }
    for (uint32_t i = checked; i < expr->call.arg_count; i++) {
        check_expr(checker, expr->call.args[i], NULL);
    }
    return callee->result;
}

static KestType *check_field(Checker *checker, KestExpr *expr) {
    // A case that carries nothing is written without brackets, so it looks
    // like a field of the enum and is the enum.
    if (expr->field.object->kind == KEST_EXPR_NAME) {
        KestSpan owner = expr->field.object->span;
        KestType *choice = kest_lookup_type(checker->program,
                                            span_text(checker, owner),
                                            owner.length);
        // One named bit, which is a value of the set it was named in.
        if (choice != NULL && choice->tag == KEST_T_FLAGS) {
            report_unimported(checker, owner);
            expr->field.object->type = choice;
            if (find_case(checker, choice, expr->field.name) == NULL) {
                return error_type(checker);
            }
            return choice;
        }
        if (choice != NULL && choice->tag == KEST_T_ENUM) {
            report_unimported(checker, owner);
            expr->field.object->type = choice;
            const KestVariantType *variant =
                find_case(checker, choice, expr->field.name);
            if (variant == NULL) {
                return error_type(checker);
            }
            if (variant->payload_count > 0) {
                report(checker, expr->span, "K0309",
                       "`%s` carries %u thing%s and was named with none",
                       variant->name, variant->payload_count,
                       variant->payload_count == 1 ? "" : "s");
            }
            return choice;
        }
    }

    KestType *object = check_expr(checker, expr->field.object, NULL);
    if (is_error(object)) {
        return error_type(checker);
    }

    const char *name = span_text(checker, expr->field.name);
    size_t length = expr->field.name.length;

    if (object->tag == KEST_T_STRUCT) {
        for (uint32_t i = 0; i < object->member_count; i++) {
            if (strlen(object->members[i].name) == length &&
                memcmp(object->members[i].name, name, length) == 0) {
                return object->members[i].type;
            }
        }
        report(checker, expr->field.name, "K0307", "`%s` has no field `%.*s`",
               type_name(checker, object), (int)length, name);
        const char *nearest = kest_nearest_member(object, name, length);
        if (nearest != NULL) {
            suggest(checker, "did you mean `%s`?", nearest);
        }
        return error_type(checker);
    }

    report(checker, expr->field.name, "K0307", "`%s` has no fields",
           type_name(checker, object));
    return error_type(checker);
}

// The elements decide the type, so the first one that resolves sets it and
// the rest are measured against it.
static KestType *check_array(Checker *checker, KestExpr *expr,
                             const KestType *expected) {
    const KestType *wanted =
        expected != NULL && expected->tag == KEST_T_ARRAY ? expected->element
                                                          : NULL;

    KestType *element = (KestType *)wanted;
    for (uint32_t i = 0; i < expr->array.count; i++) {
        KestType *item = check_expr(checker, expr->array.items[i], element);
        if (element == NULL || element->tag == KEST_T_ERROR) {
            element = item;
        } else if (!kest_type_equal(item, element)) {
            expected_but(checker, expr->array.items[i]->span, element, item,
                         "this element");
        }
    }

    if (element == NULL) {
        report(checker, expr->span, "K0320",
               "an empty array has no element type here");
        kest_diags_suggest(checker->program->diags,
                           "write it down: `let a: [i32] = []`");
        return error_type(checker);
    }
    return kest_array_of(checker->program, element);
}

static KestType *check_index(Checker *checker, KestExpr *expr) {
    KestType *object = check_expr(checker, expr->index.object, NULL);
    KestType *index = check_expr(checker, expr->index.index, NULL);

    if (!is_error(index) && index->tag != KEST_T_INT) {
        report(checker, expr->index.index->span, "K0315",
               "an index must be an integer, found `%s`",
               type_name(checker, index));
    }
    if (is_error(object)) {
        return error_type(checker);
    }
    // A piece of text is its bytes. There is no character type, so what comes
    // out is a `u8` and decoding is the program's business.
    if (object->tag == KEST_T_TEXT) {
        return builtin(checker, "u8");
    }
    if (object->tag != KEST_T_ARRAY) {
        report(checker, expr->index.object->span, "K0315",
               "`%s` cannot be indexed", type_name(checker, object));
        return error_type(checker);
    }
    return object->element;
}

static bool is_bitwise(KestTokenKind op) {
    return op == KEST_TOK_AMP || op == KEST_TOK_PIPE || op == KEST_TOK_CARET;
}

static bool is_comparison(KestTokenKind op) {
    return op == KEST_TOK_LT || op == KEST_TOK_LTEQ || op == KEST_TOK_GT ||
           op == KEST_TOK_GTEQ;
}

// The token name without the backticks it carries for diagnostics. The buffer
// is the caller's, so nothing here holds state between calls.
static const char *operator_text(KestTokenKind op, char *buffer, size_t size) {
    const char *name = kest_token_name(op);
    size_t used = 0;
    for (const char *c = name; *c != '\0' && used + 1 < size; c++) {
        if (*c != '`') {
            buffer[used++] = *c;
        }
    }
    buffer[used] = '\0';
    return buffer;
}

static KestType *check_binary(Checker *checker, KestExpr *expr,
                              const KestType *expected) {
    KestTokenKind op = expr->binary.op;
    char spelling[8];

    if (op == KEST_TOK_AMPAMP || op == KEST_TOK_PIPEPIPE) {
        KestType *boolean = builtin(checker, "bool");
        KestType *left = check_expr(checker, expr->binary.left, boolean);
        KestType *right = check_expr(checker, expr->binary.right, boolean);
        if (!kest_type_equal(left, boolean)) {
            expected_but(checker, expr->binary.left->span, boolean, left,
                         operator_text(op, spelling, sizeof(spelling)));
        }
        if (!kest_type_equal(right, boolean)) {
            expected_but(checker, expr->binary.right->span, boolean, right,
                         operator_text(op, spelling, sizeof(spelling)));
        }
        return boolean;
    }

    // A shift has a value and a count, not two operands: the count says how
    // far, the same way an index says how deep, and neither has to be the
    // type of the thing it is applied to.
    if (op == KEST_TOK_LTLT || op == KEST_TOK_GTGT) {
        KestType *value = check_expr(checker, expr->binary.left, inside(expected));
        KestType *by = check_expr(checker, expr->binary.right,
                                  builtin(checker, "i32"));
        if (!is_error(value) && value->tag != KEST_T_INT) {
            report(checker, expr->span, "K0314", "`%s` does not apply to `%s`",
                   operator_text(op, spelling, sizeof(spelling)),
                   type_name(checker, value));
            return error_type(checker);
        }
        if (!is_error(by) && by->tag != KEST_T_INT) {
            report(checker, expr->binary.right->span, "K0336",
                   "a shift counts, and a count is an integer, found `%s`",
                   type_name(checker, by));
            return is_error(value) ? error_type(checker) : value;
        }
        return value;
    }

    bool logical = is_comparison(op) || op == KEST_TOK_EQEQ ||
                   op == KEST_TOK_BANGEQ;
    const KestType *hint = logical ? NULL : inside(expected);

    KestType *left = check_expr(checker, expr->binary.left, hint);
    KestType *right = check_expr(checker, expr->binary.right, left);

    // A literal on the left takes its type from the other side, so `2.0 * dt`
    // reads the same as `dt * 2.0`. The node is corrected too: the compiler
    // reads the type from there to choose between an `f32` and an `f64`
    // instruction, and a type only the checker knows is one nobody applies.
    if (!kest_type_equal(left, right) && is_literal(expr->binary.left) &&
        !is_error(right) && left != NULL && left->tag == right->tag) {
        left = right;
        expr->binary.left->type = right;
        if (expr->binary.left->kind == KEST_EXPR_UNARY) {
            expr->binary.left->unary.operand->type = right;
        }
    }

    if (!kest_type_equal(left, right)) {
        report(checker, expr->span, "K0314",
               "`%s` needs both sides to have one type, found `%s` and `%s`",
               operator_text(op, spelling, sizeof(spelling)), type_name(checker, left),
               type_name(checker, right));
        return logical ? builtin(checker, "bool") : error_type(checker);
    }

    if (op == KEST_TOK_EQEQ || op == KEST_TOK_BANGEQ) {
        // Comparing two arrays or two structs is a question with more than one
        // answer, and the one a handle comparison gives is the wrong one.
        if (!is_error(left) && left->tag != KEST_T_INT &&
            left->tag != KEST_T_FLOAT && left->tag != KEST_T_BOOL &&
            left->tag != KEST_T_TEXT && left->tag != KEST_T_FLAGS) {
            report(checker, expr->span, "K0314",
                   "`%s` does not apply to `%s`",
                   operator_text(op, spelling, sizeof(spelling)),
                   type_name(checker, left));
            kest_diags_suggest(checker->program->diags,
                               "compare the fields that decide it");
        }
        return builtin(checker, "bool");
    }

    // A set of bits is what `&`, `|` and `^` are for, and combining two of
    // one set gives that set rather than the number under it.
    if (is_bitwise(op) && !is_error(left) && left->tag == KEST_T_FLAGS) {
        return left;
    }
    // Bits are what an integer is made of and what nothing else is made of.
    // A `bool` has `&&` and `||`, which say what they mean about one bit.
    if (is_bitwise(op) && !is_error(left) && left->tag != KEST_T_INT) {
        report(checker, expr->span, "K0314", "`%s` does not apply to `%s`",
               operator_text(op, spelling, sizeof(spelling)),
               type_name(checker, left));
        if (left != NULL && left->tag == KEST_T_BOOL) {
            kest_diags_suggest(checker->program->diags,
                               op == KEST_TOK_AMP ? "`&&` is the one for "
                                                    "`bool`"
                                                  : "`||` is the one for "
                                                    "`bool`");
        }
        return error_type(checker);
    }
    // Text has an order, by its bytes, and only the comparisons use it.
    bool orderable =
        is_numeric(left) ||
        (is_comparison(op) && left != NULL && left->tag == KEST_T_TEXT);
    if (!is_error(left) && !orderable) {
        report(checker, expr->span, "K0314", "`%s` does not apply to `%s`",
               operator_text(op, spelling, sizeof(spelling)),
               type_name(checker, left));
        return logical ? builtin(checker, "bool") : error_type(checker);
    }
    if (op == KEST_TOK_PERCENT && !is_error(left) &&
        left->tag != KEST_T_INT) {
        report(checker, expr->span, "K0314",
               "`%%` does not apply to `%s`", type_name(checker, left));
        return error_type(checker);
    }

    return logical ? builtin(checker, "bool") : left;
}

// A literal is written in a type, and one that does not fit in it is a
// mistake the reader made rather than a value the machine should wrap.
static void check_literal_fits(Checker *checker, const KestExpr *expr,
                               const KestType *type) {
    if (type == NULL || type->tag != KEST_T_INT) {
        return;
    }

    bool overflow = false;
    uint64_t value = kest_token_integer(span_text(checker, expr->span),
                                        expr->span.length, &overflow);

    uint64_t limit;
    if (!type->is_signed) {
        if (checker->negating) {
            report(checker, expr->span, "K0326",
                   "`%s` holds no negative numbers", type_name(checker, type));
            return;
        }
        limit = type->width == 64 ? UINT64_MAX
                                  : ((uint64_t)1 << type->width) - 1;
    } else {
        // One further down than up, which is why the sign is part of the
        // question rather than applied to the answer.
        limit = (uint64_t)1 << (type->width - 1);
        if (!checker->negating) {
            limit -= 1;
        }
    }

    if (overflow || value > limit) {
        report(checker, expr->span, "K0326", "%s%.*s does not fit in `%s`",
               checker->negating ? "-" : "", (int)expr->span.length,
               span_text(checker, expr->span), type_name(checker, type));
    }
}

static void check_block(Checker *checker, KestBlock *block);

// Whether every arm gives the same thing, which is what makes the match one
// thing rather than several.
static KestType *check_branch(Checker *checker, KestExpr *expr,
                              const KestType *expected);

static KestType *check_match(Checker *checker, KestExpr *expr,
                             const KestType *expected) {
    KestChoose *choose = &expr->choose;
    KestType *subject = check_expr(checker, choose->subject, NULL);
    if (!is_error(subject) && subject->tag != KEST_T_ENUM) {
        report(checker, choose->subject->span, "K0331",
               "`match` chooses between the cases of an enum, found `%s`",
               type_name(checker, subject));
        subject = error_type(checker);
    }

    bool seen[64] = {false};
    bool has_else = false;
    KestType *given = NULL;

    for (uint32_t a = 0; a < choose->arm_count; a++) {
        KestArm *arm = &choose->arms[a];
        const KestVariantType *variant = NULL;

        if (arm->name.length == 0) {
            if (has_else) {
                report(checker, expr->span, "K0332",
                       "this `match` has two `else` arms");
            }
            has_else = true;
        } else if (!is_error(subject)) {
            variant = find_case(checker, subject, arm->name);
            if (variant != NULL) {
                uint32_t which = (uint32_t)(variant - subject->cases);
                if (which < 64 && seen[which]) {
                    report(checker, arm->name, "K0332",
                           "`%s` is already answered here", variant->name);
                }
                if (which < 64) {
                    seen[which] = true;
                }
                if (arm->binding_count != variant->payload_count) {
                    report(checker, arm->name, "K0309",
                           "`%s` carries %u thing%s, and %u name%s given",
                           variant->name, variant->payload_count,
                           variant->payload_count == 1 ? "" : "s",
                           arm->binding_count,
                           arm->binding_count == 1 ? " was" : "s were");
                }
            }
        }

        uint32_t mark = checker->local_count;
        checker->depth++;
        for (uint32_t b = 0; b < arm->binding_count; b++) {
            declare_local(checker, arm->bindings[b],
                          variant != NULL && b < variant->payload_count
                              ? variant->payload[b]
                              : error_type(checker));
        }
        if (arm->value != NULL) {
            KestType *value = check_expr(checker, arm->value,
                                         given != NULL ? given : expected);
            // The first arm that has a type of its own settles what the match
            // is; a literal takes it, the way a literal always does.
            if (given == NULL ||
                (!takes_a_type(arm->value) && takes_a_type(
                     choose->arms[0].value) && given->tag == value->tag)) {
                given = value;
            }
        } else {
            check_block(checker, &arm->body);
        }
        checker->depth--;
        checker->local_count = mark;
    }

    // Every case answered, or an `else` saying the rest are one answer.
    choose->total = has_else;
    if (!is_error(subject) && !has_else) {
        bool all = true;
        for (uint32_t c = 0; c < subject->case_count && c < 64; c++) {
            if (!seen[c]) {
                all = false;
                report(checker, expr->span, "K0333",
                       "this `match` does not answer `%s`",
                       subject->cases[c].name);
                kest_diags_note(checker->program->diags, subject->declared_in,
                                subject->cases[c].span, "this case");
            }
        }
        choose->total = all;
    }

    if (!choose->gives) {
        return builtin(checker, "void");
    }
    if (given == NULL) {
        return error_type(checker);
    }
    // Now that what it gives is settled, every arm has to give that.
    for (uint32_t a = 0; a < choose->arm_count; a++) {
        KestExpr *value = choose->arms[a].value;
        if (value == NULL) {
            continue;
        }
        if (takes_a_type(value) && value->type != NULL &&
            value->type->tag == given->tag) {
            value->type = given;
            continue;
        }
        if (!kest_type_equal(value->type, given)) {
            expected_but(checker, value->span, given, value->type, "this arm");
        }
    }
    return given;
}

static KestType *check_expr_kind(Checker *checker, KestExpr *expr,
                                 const KestType *expected) {
    switch (expr->kind) {
    case KEST_EXPR_INT: {
        KestType *type = expected != NULL && expected->tag == KEST_T_INT
                             ? (KestType *)expected
                             : builtin(checker, "i32");
        check_literal_fits(checker, expr, type);
        return type;
    }

    case KEST_EXPR_FLOAT:
        if (expected != NULL && expected->tag == KEST_T_FLOAT) {
            return (KestType *)expected;
        }
        // f32 is the working precision of the workloads this language is for.
        return builtin(checker, "f32");

    case KEST_EXPR_STRING:
        return builtin(checker, "text");

    case KEST_EXPR_BOOL:
        return builtin(checker, "bool");

    case KEST_EXPR_NONE:
        if (expected == NULL || expected->tag != KEST_T_OPTIONAL) {
            report(checker, expr->span, "K0322",
                   "`none` has no type here");
            kest_diags_suggest(checker->program->diags,
                               "write what it is missing: `let x: i32? = none`");
            return error_type(checker);
        }
        return (KestType *)expected;

    case KEST_EXPR_NAME:
        return check_name(checker, expr);

    case KEST_EXPR_UNARY: {
        if (expr->unary.op == KEST_TOK_BANG) {
            KestType *boolean = builtin(checker, "bool");
            KestType *operand = check_expr(checker, expr->unary.operand, boolean);
            if (!kest_type_equal(operand, boolean)) {
                expected_but(checker, expr->unary.operand->span, boolean,
                             operand, "`!`");
            }
            return boolean;
        }
        if (expr->unary.op == KEST_TOK_TILDE) {
            KestType *operand =
                check_expr(checker, expr->unary.operand, inside(expected));
            if (!is_error(operand) && operand->tag != KEST_T_INT &&
                operand->tag != KEST_T_FLAGS) {
                report(checker, expr->span, "K0314",
                       "`~` does not apply to `%s`",
                       type_name(checker, operand));
                if (operand != NULL && operand->tag == KEST_T_BOOL) {
                    kest_diags_suggest(checker->program->diags,
                                       "`!` is the one for `bool`");
                }
                return error_type(checker);
            }
            return operand;
        }
        bool was_negating = checker->negating;
        checker->negating = expr->unary.operand->kind == KEST_EXPR_INT;
        KestType *operand =
            check_expr(checker, expr->unary.operand, inside(expected));
        checker->negating = was_negating;
        if (!is_error(operand) && !is_numeric(operand)) {
            report(checker, expr->span, "K0314", "`-` does not apply to `%s`",
                   type_name(checker, operand));
            return error_type(checker);
        }
        return operand;
    }

    case KEST_EXPR_BINARY:
        return check_binary(checker, expr, expected);

    case KEST_EXPR_CALL:
        return check_call(checker, expr, expected);

    case KEST_EXPR_FIELD:
        return check_field(checker, expr);

    case KEST_EXPR_INDEX:
        return check_index(checker, expr);

    case KEST_EXPR_ARRAY:
        return check_array(checker, expr, expected);

    case KEST_EXPR_MATCH:
        return check_match(checker, expr, expected);

    case KEST_EXPR_IF:
        return check_branch(checker, expr, expected);

    case KEST_EXPR_TEXT: {
        for (uint32_t i = 0; i < expr->text.count; i++) {
            KestExpr *hole = expr->text.parts[i].value;
            if (hole == NULL) {
                continue;
            }
            KestType *type = check_expr(checker, hole, NULL);
            // Only what has one obvious spelling is written for you. A struct
            // has several and the author knows which one they meant.
            // A set of bits has one obvious spelling now that it can be
            // walked: the flags it holds, written the way they are written.
            if (!is_error(type) && type->tag != KEST_T_INT &&
                type->tag != KEST_T_FLOAT && type->tag != KEST_T_BOOL &&
                type->tag != KEST_T_TEXT && type->tag != KEST_T_FLAGS) {
                report(checker, hole->span, "K0324",
                       "there is no text for `%s`", type_name(checker, type));
                kest_diags_suggest(
                    checker->program->diags,
                    type->tag == KEST_T_ENUM
                        ? "a `match` gives text for the case it is"
                        : "write the fields you want to see");
            }
        }
        return builtin(checker, "text");
    }
    }
    return error_type(checker);
}

// Every expression is typed here and nowhere else, so the compiler can read
// `expr->type` for any node the checker walked.
static KestType *check_expr(Checker *checker, KestExpr *expr,
                            const KestType *expected) {
    if (expr == NULL) {
        return error_type(checker);
    }
    KestType *type = check_expr_kind(checker, expr, expected);

    // A value standing where an optional is wanted becomes one. It is the
    // only conversion the language does, and it loses nothing.
    if (expected != NULL && expected->tag == KEST_T_OPTIONAL && type != NULL &&
        type->tag != KEST_T_OPTIONAL && type->tag != KEST_T_ERROR &&
        kest_type_equal(type, expected->element)) {
        expr->wrapped = true;
        type = (KestType *)expected;
    }

    expr->type = type;
    return type;
}

static void check_block(Checker *checker, KestBlock *block);

static bool is_constant_target(Checker *checker, KestExpr *target) {
    if (target->kind != KEST_EXPR_NAME) {
        return false;
    }
    const char *name = span_text(checker, target->span);
    if (find_local(checker, name, target->span.length) != NULL) {
        return false;
    }
    KestSymbol *global =
        kest_find_global(checker->program, name, target->span.length);
    return global != NULL && global->is_const;
}

static void check_condition(Checker *checker, KestExpr *condition,
                            const char *where) {
    KestType *boolean = builtin(checker, "bool");
    KestType *type = check_expr(checker, condition, boolean);
    if (!kest_type_equal(type, boolean)) {
        report(checker, condition->span, "K0312",
               "a %s condition must be `bool`, found `%s`", where,
               type_name(checker, type));
    }
}

// An `if` is checked the same whichever it is used as, because the arms say
// which it is. Giving arms have to agree on a type and there has to be an
// `else`, since a value has to exist on both ways through.
static KestType *check_branch(Checker *checker, KestExpr *expr,
                              const KestType *expected) {
    KestBranch *branch = expr->branch;
    KestType *held = NULL;

    if (branch->binding.length == 0) {
        check_condition(checker, branch->condition, "`if`");
    } else {
        KestType *optional = check_expr(checker, branch->condition, NULL);
        held = error_type(checker);
        if (!is_error(optional)) {
            if (optional->tag == KEST_T_OPTIONAL) {
                held = optional->element;
            } else {
                report(checker, branch->condition->span, "K0323",
                       "`if let` opens an optional, found `%s`",
                       type_name(checker, optional));
            }
        }
    }

    // The name exists only where the value did, which is what makes the
    // failure impossible to ignore rather than merely rude to.
    uint32_t mark = checker->local_count;
    checker->depth++;
    if (held != NULL) {
        declare_local(checker, branch->binding, held);
    }
    KestType *given = NULL;
    if (branch->then_value != NULL) {
        given = check_expr(checker, branch->then_value, expected);
    } else {
        check_block(checker, &branch->then_body);
    }
    checker->depth--;
    checker->local_count = mark;

    KestType *other = NULL;
    if (branch->otherwise != NULL) {
        other = check_expr(checker, branch->otherwise,
                           given != NULL ? given : expected);
    } else if (branch->has_else) {
        if (branch->else_value != NULL) {
            other = check_expr(checker, branch->else_value,
                               given != NULL ? given : expected);
        } else {
            checker->depth++;
            check_block(checker, &branch->else_body);
            checker->depth--;
            checker->local_count = mark;
        }
    }

    if (!branch->gives) {
        return builtin(checker, "void");
    }
    if (!branch->has_else) {
        report(checker, expr->span, "K0334",
               "an `if` that gives a value needs an `else`");
        kest_diags_suggest(checker->program->diags,
                           "there has to be a value on both ways through");
        return given != NULL ? given : error_type(checker);
    }
    if (given == NULL) {
        return error_type(checker);
    }
    // A literal in one arm takes the shape the other arm settled on, which is
    // what makes `if c -> 1 else -> x` work when `x` is an `f32`.
    if (takes_a_type(branch->then_value) && other != NULL &&
        !is_error(other) && other->tag == given->tag) {
        given = other;
        branch->then_value->type = given;
    }
    KestExpr *second = branch->otherwise != NULL ? branch->otherwise
                                                 : branch->else_value;
    if (second != NULL) {
        if (takes_a_type(second) && second->type != NULL &&
            second->type->tag == given->tag) {
            second->type = given;
        } else if (!kest_type_equal(second->type, given)) {
            expected_but(checker, second->span, given, second->type,
                         "this arm");
        }
    }
    return given;
}

static void check_stmt(Checker *checker, KestStmt *stmt) {
    switch (stmt->kind) {
    case KEST_STMT_LET: {
        KestType *declared = NULL;
        if (stmt->let.type != NULL) {
            declared = kest_resolve_type_ref(checker->program, stmt->let.type);
        }
        KestType *value = check_expr(checker, stmt->let.value, declared);
        if (declared != NULL && !kest_type_equal(value, declared)) {
            expected_but(checker, stmt->let.value->span, declared, value,
                         "this binding");
        }
        declare_local(checker, stmt->let.name,
                      declared != NULL ? declared : value);
        break;
    }

    case KEST_STMT_ASSIGN: {
        char spelling[8];
        KestType *target = check_expr(checker, stmt->assign.target, NULL);
        KestType *value = check_expr(checker, stmt->assign.value, target);
        const KestExpr *root = NULL;
        bool is_index = false;
        if (writes_only_a_copy(checker, stmt->assign.target, &root,
                               &is_index)) {
            kest_diags_add(checker->program->diags, KEST_SEVERITY_WARNING,
                           "K0321", stmt->assign.target->span,
                           "`%.*s` is the loop's own, so this is discarded",
                           (int)root->span.length,
                           span_text(checker, root->span));
            // What to do instead depends on what is being walked, and the
            // three answers are different enough that one of them would be
            // wrong for the other two.
            const KestType *walked = target;
            kest_diags_suggest(
                checker->program->diags,
                is_index ? "the walk keeps its own count, which this is a "
                           "copy of"
                : walked != NULL && walked->tag == KEST_T_FLAGS
                    ? "the walk gives one bit at a time; build the set you "
                      "want"
                    : "index the array to write to it: `a[i]` names the "
                      "element");
        }
        if (is_constant_target(checker, stmt->assign.target)) {
            report(checker, stmt->assign.target->span, "K0311",
                   "`%.*s` is a constant",
                   (int)stmt->assign.target->span.length,
                   span_text(checker, stmt->assign.target->span));
        }
        if (!kest_type_equal(target, value)) {
            expected_but(checker, stmt->assign.value->span, target, value,
                         "this assignment");
        }
        if (stmt->assign.op != KEST_TOK_EQ && !is_error(target) &&
            !is_numeric(target)) {
            report(checker, stmt->span, "K0314", "`%s` does not apply to `%s`",
                   operator_text(stmt->assign.op, spelling, sizeof(spelling)), type_name(checker, target));
        }
        break;
    }

    case KEST_STMT_EXPR:
        check_expr(checker, stmt->value, NULL);
        break;

    case KEST_STMT_WHILE:
        check_condition(checker, stmt->loop.condition, "`while`");
        checker->loop_depth++;
        check_block(checker, &stmt->loop.body);
        checker->loop_depth--;
        break;

    case KEST_STMT_FOR: {
        KestType *sequence = check_expr(checker, stmt->each.sequence, NULL);
        KestType *element = error_type(checker);
        if (!is_error(sequence)) {
            if (sequence->tag == KEST_T_ARRAY) {
                element = sequence->element;
            } else if (sequence->tag == KEST_T_STORE) {
                if (stmt->each.index.length > 0) {
                    report(checker, stmt->each.index, "K0317",
                           "a store has no positions to walk by");
                    kest_diags_suggest(checker->program->diags,
                                       "the reference is what names a slot");
                }
                // What a walk of a store has to give is a reference, because
                // a reference is what removing and writing take. The value is
                // a `get` away, and that `get` returns an optional it cannot
                // fail, which is the noise probe 4 asked about; see D020.
                element = kest_ref_of(checker->program, sequence->element);
            } else if (sequence->tag == KEST_T_FLAGS) {
                if (stmt->each.index.length > 0) {
                    report(checker, stmt->each.index, "K0317",
                           "a set of bits has no positions to walk by");
                    kest_diags_suggest(checker->program->diags,
                                       "the flag is what names the bit");
                }
                // What is walked is the flags that are set, each one a value
                // of the set with that one bit in it, so nothing has to be
                // asked about what came out.
                element = sequence;
            } else {
                report(checker, stmt->each.sequence->span, "K0317",
                       "`for` walks an array, a store or a set of bits, "
                       "found `%s`",
                       type_name(checker, sequence));
            }
        }
        uint32_t mark = checker->local_count;
        checker->depth++;
        if (stmt->each.index.length > 0) {
            declare_local(checker, stmt->each.index, builtin(checker, "i32"));
            if (checker->local_count > mark) {
                checker->locals[checker->local_count - 1].is_loop_element =
                    true;
                checker->locals[checker->local_count - 1].is_loop_index = true;
            }
        }
        uint32_t before_element = checker->local_count;
        declare_local(checker, stmt->each.name, element);
        if (checker->local_count > before_element) {
            checker->locals[checker->local_count - 1].is_loop_element = true;
        }
        checker->loop_depth++;
        check_block(checker, &stmt->each.body);
        checker->loop_depth--;
        checker->depth--;
        checker->local_count = mark;
        break;
    }

    case KEST_STMT_RETURN: {
        KestType *want = checker->result;
        if (stmt->result == NULL) {
            if (want != NULL && want->tag != KEST_T_VOID) {
                report(checker, stmt->span, "K0310",
                       "this function returns `%s`, so `return` needs a value",
                       type_name(checker, want));
            }
            break;
        }
        KestType *value = check_expr(checker, stmt->result, want);
        if (want != NULL && want->tag == KEST_T_VOID) {
            report(checker, stmt->result->span, "K0310",
                   "this function returns nothing, so `return` takes no value");
        } else if (!kest_type_equal(value, want)) {
            expected_but(checker, stmt->result->span, want, value,
                         "this return");
        }
        break;
    }

    case KEST_STMT_BREAK:
    case KEST_STMT_CONTINUE:
        if (checker->loop_depth == 0) {
            report(checker, stmt->span, "K0313", "`%s` is outside a loop",
                   stmt->kind == KEST_STMT_BREAK ? "break" : "continue");
        }
        break;

    case KEST_STMT_BLOCK:
        check_block(checker, &stmt->block);
        break;
    }
}

static void check_block(Checker *checker, KestBlock *block) {
    uint32_t mark = checker->local_count;
    checker->depth++;
    for (uint32_t i = 0; i < block->count; i++) {
        check_stmt(checker, block->items[i]);
        if (checker->out_of_memory) {
            break;
        }
    }
    checker->depth--;
    // Dropping the scope is what lets a sibling block reuse a name.
    checker->local_count = mark;
}

// Whether control cannot fall off the end of a block. A branch counts only
// when both of its arms do, because a missing `else` is a path.
static bool stmt_returns(const KestStmt *stmt);

static bool always_returns(const KestBlock *block) {
    return block->count > 0 && stmt_returns(block->items[block->count - 1]);
}

// A `match` or an `if` that leaves through every arm is a thing that returns,
// and the line after it is unreachable rather than required.
static bool expr_returns(const KestExpr *value) {
    if (value == NULL) {
        return false;
    }
    if (value->kind == KEST_EXPR_IF) {
        const KestBranch *branch = value->branch;
        if (!branch->has_else || branch->gives ||
            !always_returns(&branch->then_body)) {
            return false;
        }
        if (branch->otherwise != NULL) {
            return expr_returns(branch->otherwise);
        }
        return always_returns(&branch->else_body);
    }
    if (value->kind != KEST_EXPR_MATCH || !value->choose.total ||
        value->choose.arm_count == 0) {
        return false;
    }
    for (uint32_t a = 0; a < value->choose.arm_count; a++) {
        if (value->choose.arms[a].value != NULL ||
            !always_returns(&value->choose.arms[a].body)) {
            return false;
        }
    }
    return true;
}

static bool stmt_returns(const KestStmt *stmt) {
    switch (stmt->kind) {
    case KEST_STMT_RETURN:
        return true;
    case KEST_STMT_BLOCK:
        return always_returns(&stmt->block);
    case KEST_STMT_EXPR:
        return expr_returns(stmt->value);
    default:
        return false;
    }
}

static bool check_unit(KestProgram *program, KestUnit *unit);

static bool check_unit(KestProgram *program, KestUnit *unit) {
    Checker checker = {0};
    checker.program = program;

    // A constant's value is an expression like any other and needs a type on
    // it, both to be measured against what was declared and because the
    // compiler writes it into every use and reads that type to choose the
    // instruction.
    for (uint32_t i = 0; i < unit->count; i++) {
        KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_CONST) {
            continue;
        }
        KestType *declared = kest_resolve_type_ref(program, decl->constant.type);
        KestType *value = check_expr(&checker, decl->constant.value, declared);
        if (!kest_type_equal(value, declared)) {
            expected_but(&checker, decl->constant.value->span, declared, value,
                         "this constant");
        }
    }

    for (uint32_t i = 0; i < unit->count; i++) {
        KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_FN || decl->function.is_extern) {
            continue;
        }

        const char *name = program->source->text + decl->name.offset;
        // Where it is declared, not what it is called: two functions may share
        // a name and each has to be checked against its own signature.
        KestSymbol *symbol =
            kest_symbol_at(program, program->source, decl->name);
        if (symbol == NULL || symbol->type->tag != KEST_T_FN) {
            continue;
        }

        checker.local_count = 0;
        checker.depth = 0;
        checker.loop_depth = 0;
        checker.result = symbol->type->result;

        for (uint32_t p = 0; p < decl->function.param_count &&
                             p < symbol->type->param_count;
             p++) {
            declare_local(&checker, decl->function.params[p]->name,
                          symbol->type->params[p]);
        }

        check_block(&checker, &decl->function.body);

        if (checker.result != NULL && checker.result->tag != KEST_T_VOID &&
            !always_returns(&decl->function.body)) {
            report(&checker, decl->name, "K0316",
                   "`%.*s` can end without returning `%s`",
                   (int)decl->name.length, name,
                   kest_type_name(program->arena, checker.result));
        }

        if (checker.out_of_memory) {
            return false;
        }
    }
    return true;
}

bool kest_check_bodies(KestProgram *program, KestUnits *units) {
    for (uint32_t u = 0; u < units->count; u++) {
        kest_program_in(program, &units->items[u]);
        kest_diags_in(program->diags, program->source);
        if (!check_unit(program, &units->items[u].unit)) {
            return false;
        }
    }
    return true;
}

