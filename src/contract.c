#include "contract.h"

#include <string.h>

#define NO_SITE ((KestSpan){0, 0})

typedef struct {
    const KestDecl *decl;
    // What it is matched by, which includes what it takes, and what it is
    // called in a message, which does not.
    const char *name;
    const char *display;
    // Where this body allocates, if it does directly. Zero length when it
    // does not.
    KestSpan site;
    // What the site is: a call through a value nothing promises about, rather
    // than an allocation. The shape is what the value is written as, which is
    // what a promise would have to be written into.
    const char *shape;
    // Indices of the functions this one calls, and where each call is.
    uint32_t *callees;
    KestSpan *calls;
    uint32_t call_count;
    uint32_t call_capacity;
    uint32_t unit;
    bool allocates;
    bool promises;
    bool is_extern;
    bool visiting;
} Function;

typedef struct {
    KestProgram *program;
    Function *functions;
    uint32_t count;
    bool out_of_memory;
} Graph;


static const char *span_text(Graph *graph, KestSpan span) {
    return graph->program->source->text + span.offset;
}

static int32_t find_exact(Graph *graph, const char *text, size_t length) {
    for (uint32_t i = 0; i < graph->count; i++) {
        if (strlen(graph->functions[i].name) == length &&
            memcmp(graph->functions[i].name, text, length) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

// Which function a call reaches, which the checker settled and left on the
// callee, because two functions may share a name.
static int32_t find_called(Graph *graph, const KestExpr *callee) {
    if (callee->type == NULL || callee->type->tag != KEST_T_FN ||
        callee->type->symbol == NULL) {
        return -1;
    }
    return find_exact(graph, callee->type->symbol,
                      strlen(callee->type->symbol));
}

static void record_call(Graph *graph, Function *caller, uint32_t callee,
                        KestSpan span) {
    if (caller->call_count == caller->call_capacity) {
        uint32_t grown =
            caller->call_capacity == 0 ? 8 : caller->call_capacity * 2;
        uint32_t *callees =
            KEST_ARENA_ARRAY(graph->program->arena, uint32_t, grown);
        KestSpan *calls =
            KEST_ARENA_ARRAY(graph->program->arena, KestSpan, grown);
        if (callees == NULL || calls == NULL) {
            graph->out_of_memory = true;
            return;
        }
        if (caller->call_count > 0) {
            memcpy(callees, caller->callees,
                   sizeof(uint32_t) * caller->call_count);
            memcpy(calls, caller->calls, sizeof(KestSpan) * caller->call_count);
        }
        caller->callees = callees;
        caller->calls = calls;
        caller->call_capacity = grown;
    }
    caller->calls[caller->call_count] = span;
    caller->callees[caller->call_count++] = callee;
}

static void walk_block(Graph *graph, Function *function,
                       const KestBlock *block);

static void walk_expr(Graph *graph, Function *function, const KestExpr *expr) {
    if (expr == NULL) {
        return;
    }

    switch (expr->kind) {
    case KEST_EXPR_ARRAY:
        // A run of a written length is laid out where it stands (D064): slots
        // in the frame, or bytes inside the struct it is written into. Nothing
        // reaches the heap, and the compiler emits no instruction that could.
        // What allocates is the kind that can grow.
        if (expr->type == NULL || expr->type->tag != KEST_T_FIXED) {
            if (function->site.length == 0) {
                function->site = expr->span;
            }
            function->allocates = true;
        }
        for (uint32_t i = 0; i < expr->array.count; i++) {
            walk_expr(graph, function, expr->array.items[i]);
        }
        break;

    case KEST_EXPR_TEXT:
        // Text with a hole in it is built, and building it reaches the heap.
        // A string with nothing in it is a constant and does not.
        if (function->site.length == 0) {
            function->site = expr->span;
        }
        function->allocates = true;
        for (uint32_t i = 0; i < expr->text.count; i++) {
            walk_expr(graph, function, expr->text.parts[i].value);
        }
        break;
    case KEST_EXPR_CALL: {
        const KestExpr *callee = expr->call.callee;
        // A store can grow, so putting something into one reaches the heap.
        // Reading through a reference, writing through one and removing what
        // it named do not, which is what makes a frame step able to walk an
        // object graph inside a promise.
        if (callee->kind == KEST_EXPR_NAME && find_called(graph, callee) < 0) {
            const char *text = span_text(graph, callee->span);
            bool allocating =
                (callee->span.length == 5 && memcmp(text, "store", 5) == 0) ||
                (callee->span.length == 5 && memcmp(text, "array", 5) == 0) ||
                (callee->span.length == 4 && memcmp(text, "push", 4) == 0) ||
                (callee->span.length == 5 && memcmp(text, "slice", 5) == 0) ||
                (callee->span.length == 3 && memcmp(text, "add", 3) == 0) ||
                // Text from bytes copies them, which is the whole point of
                // it: the pieces are gathered free and paid for once.
                (callee->span.length == 4 && memcmp(text, "text", 4) == 0);
            if (allocating) {
                if (function->site.length == 0) {
                    function->site = expr->span;
                }
                function->allocates = true;
            }
        }
        // Through a value there is no body to follow, so what it promises is
        // what is known about it. A function type with no symbol is a value
        // rather than a declaration, and its promise is part of its type,
        // which is what keeps this provable at all.
        if (callee->type != NULL && callee->type->tag == KEST_T_FN &&
            callee->type->symbol == NULL && !callee->type->is_foreign &&
            !callee->type->no_alloc) {
            if (function->site.length == 0) {
                function->site = expr->span;
                function->shape =
                    kest_type_name(graph->program->arena, callee->type);
            }
            function->allocates = true;
        }
        // Building a struct is not a call and does not reach anything. A
        // dotted callee is an extern named for its host type.
        int32_t index = find_called(graph, callee);
        if (index >= 0) {
            record_call(graph, function, (uint32_t)index, expr->span);
        }
        for (uint32_t i = 0; i < expr->call.arg_count; i++) {
            walk_expr(graph, function, expr->call.args[i]);
        }
        break;
    }
    case KEST_EXPR_UNARY:
        walk_expr(graph, function, expr->unary.operand);
        break;
    case KEST_EXPR_BINARY:
        walk_expr(graph, function, expr->binary.left);
        walk_expr(graph, function, expr->binary.right);
        break;
    case KEST_EXPR_FIELD:
        walk_expr(graph, function, expr->field.object);
        break;
    case KEST_EXPR_INDEX:
        walk_expr(graph, function, expr->index.object);
        walk_expr(graph, function, expr->index.index);
        break;
    case KEST_EXPR_MATCH:
        for (uint32_t i = 0; i < expr->choose.subject_count; i++) {
            walk_expr(graph, function, expr->choose.subjects[i]);
        }
        for (uint32_t a = 0; a < expr->choose.arm_count; a++) {
            walk_expr(graph, function, expr->choose.arms[a].value);
            walk_block(graph, function, &expr->choose.arms[a].body);
        }
        break;
    case KEST_EXPR_IF:
        walk_expr(graph, function, expr->branch->condition);
        walk_expr(graph, function, expr->branch->then_value);
        walk_block(graph, function, &expr->branch->then_body);
        walk_expr(graph, function, expr->branch->otherwise);
        walk_expr(graph, function, expr->branch->else_value);
        walk_block(graph, function, &expr->branch->else_body);
        break;
    default:
        break;
    }
}

static void walk_stmt(Graph *graph, Function *function, const KestStmt *stmt) {
    switch (stmt->kind) {
    case KEST_STMT_LET:
        walk_expr(graph, function, stmt->let.value);
        break;
    case KEST_STMT_ASSIGN:
        walk_expr(graph, function, stmt->assign.target);
        walk_expr(graph, function, stmt->assign.value);
        break;
    case KEST_STMT_EXPR:
    case KEST_STMT_DEFER:
        // What is deferred still runs, so it counts against the promise.
        walk_expr(graph, function, stmt->value);
        break;
    case KEST_STMT_WHILE:
        walk_expr(graph, function, stmt->loop.condition);
        walk_block(graph, function, &stmt->loop.body);
        break;
    case KEST_STMT_FOR:
        walk_expr(graph, function, stmt->each.sequence);
        walk_expr(graph, function, stmt->each.until);
        walk_block(graph, function, &stmt->each.body);
        break;
    case KEST_STMT_RETURN:
        walk_expr(graph, function, stmt->result);
        break;
    case KEST_STMT_BLOCK:
        walk_block(graph, function, &stmt->block);
        break;
    default:
        break;
    }
}

static void walk_block(Graph *graph, Function *function,
                       const KestBlock *block) {
    for (uint32_t i = 0; i < block->count; i++) {
        walk_stmt(graph, function, block->items[i]);
    }
}

// Follows the calls down to a body that allocates, collecting the names it
// went through. Reporting where the promise was made leaves the reader to
// walk the graph; reporting where the allocation is does not.
#define MAX_PATH 16

typedef struct {
    const char *names[MAX_PATH];
    // Where each call is, and in which file, so the path is a place per hop
    // rather than a sentence.
    KestSpan calls[MAX_PATH];
    uint32_t units[MAX_PATH];
    uint32_t count;
    KestSpan site;
    // Set when the site is a call through a value rather than an allocation:
    // the shape the value is written as. What is wrong with it is not that it
    // allocates but that nothing says it does not.
    const char *shape;
    // Which file the site is in. A span alone does not say, and the body that
    // breaks a promise is often not in the file that made it.
    uint32_t unit;
    // Set when the path ends at a foreign function rather than at a body,
    // because then there is a declaration to point at rather than a line.
    bool ends_in_extern;
} Path;

static bool trace(Graph *graph, uint32_t index, Path *path) {
    Function *function = &graph->functions[index];
    if (function->visiting || path->count == MAX_PATH) {
        return false;
    }

    if (function->site.length > 0) {
        path->site = function->site;
        path->unit = function->unit;
        path->shape = function->shape;
        return true;
    }
    if (function->is_extern) {
        return false;
    }

    function->visiting = true;
    for (uint32_t i = 0; i < function->call_count; i++) {
        Function *callee = &graph->functions[function->callees[i]];
        if (!callee->allocates) {
            continue;
        }

        path->calls[path->count] = function->calls[i];
        path->units[path->count] = function->unit;
        path->names[path->count++] = callee->display;
        if (callee->is_extern) {
            path->site = function->calls[i];
            path->unit = function->unit;
            path->ends_in_extern = true;
            function->visiting = false;
            return true;
        }
        if (trace(graph, function->callees[i], path)) {
            function->visiting = false;
            return true;
        }
        path->count--;
    }
    function->visiting = false;
    return false;
}

bool kest_check_contracts(KestProgram *program, const KestUnits *units) {
    Graph graph = {0};
    graph.program = program;

    for (uint32_t u = 0; u < units->count; u++) {
        for (uint32_t i = 0; i < units->items[u].unit.count; i++) {
            if (units->items[u].unit.items[i]->kind == KEST_DECL_FN) {
                graph.count++;
            }
        }
    }
    if (graph.count == 0) {
        return true;
    }
    graph.functions = KEST_ARENA_ARRAY(program->arena, Function, graph.count);
    if (graph.functions == NULL) {
        return false;
    }

    uint32_t next = 0;
    for (uint32_t u = 0; u < units->count; u++) {
      kest_program_in(program, &units->items[u]);
      const KestUnit *unit = &units->items[u].unit;
      for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_FN) {
            continue;
        }
        Function *function = &graph.functions[next++];
        function->unit = u;
        function->decl = decl;
        // Named by the symbol the checker gave it, which includes what it
        // takes, because two functions may share a name. An extern is
        // declared under its receiver too, so that is where it is found.
        KestSpan where = decl->name;
        if (decl->function.receiver.length > 0) {
            where.offset = decl->function.receiver.offset;
            where.length = decl->name.offset + decl->name.length -
                           decl->function.receiver.offset;
        }
        KestSymbol *symbol = kest_symbol_at(program, program->source, where);
        if (symbol == NULL || symbol->type->symbol == NULL) {
            next--;
            graph.count--;
            continue;
        }
        function->name = symbol->type->symbol;
        function->display = symbol->name;
        function->promises = decl->function.no_alloc;
        function->is_extern = decl->function.is_extern;
        // A foreign body is not here to be read, so its promise is the only
        // thing there is to go on.
        function->allocates = function->is_extern && !function->promises;
        function->site = NO_SITE;
      }
    }

    for (uint32_t i = 0; i < graph.count; i++) {
        Function *function = &graph.functions[i];
        kest_program_in(program, &units->items[function->unit]);
        if (!function->is_extern) {
            walk_block(&graph, function, &function->decl->function.body);
        }
        if (graph.out_of_memory) {
            return false;
        }
    }

    // Allocation spreads up the call graph until nothing changes, which is
    // what makes a promise about a whole call tree rather than one body.
    bool moved = true;
    while (moved) {
        moved = false;
        for (uint32_t i = 0; i < graph.count; i++) {
            Function *function = &graph.functions[i];
            if (function->allocates) {
                continue;
            }
            for (uint32_t c = 0; c < function->call_count; c++) {
                if (graph.functions[function->callees[c]].allocates) {
                    function->allocates = true;
                    moved = true;
                    break;
                }
            }
        }
    }

    for (uint32_t i = 0; i < graph.count; i++) {
        Function *function = &graph.functions[i];
        if (!function->promises || !function->allocates ||
            function->is_extern) {
            continue;
        }

        Path path = {0};
        path.unit = function->unit;
        if (!trace(&graph, i, &path)) {
            path.site = function->decl->name;
        }
        kest_program_in(program, &units->items[path.unit]);
        kest_diags_in(program->diags, program->source);

        if (path.shape != NULL) {
            // Not that it allocates: that nothing says it does not. The fix
            // is in the shape the value is written as, which is where a
            // promise about a body nobody can see has to live.
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0402",
                           path.site,
                           "nothing promises about what this calls, and `%s` "
                           "promises `no.alloc`",
                           function->display);
            kest_diags_suggest(program->diags,
                               "write the promise into the shape: "
                               "`%s no.alloc`",
                               path.shape);
        } else {
            kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0401",
                           path.site,
                           "this allocates, and `%s` promises `no.alloc`",
                           function->display);
        }

        if (path.ends_in_extern) {
            kest_diags_suggest(program->diags, "`%s` is declared to allocate",
                               path.names[path.count - 1]);
        }

        // The promise first, then the calls under it in the order they are
        // made, so the chain reads forwards from the thing that was promised
        // to the thing that breaks it.
        kest_diags_note(program->diags, &units->items[function->unit].source,
                        function->decl->name, "`%s` promises it here",
                        function->display);
        uint32_t hops = path.ends_in_extern ? path.count - 1 : path.count;
        for (uint32_t n = 0; n < hops; n++) {
            kest_diags_note(program->diags, &units->items[path.units[n]].source,
                            path.calls[n], "which calls `%s`",
                            path.names[n]);
        }
    }
    return true;
}
