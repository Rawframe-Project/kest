#include "contract.h"

#include <string.h>

#define NO_SITE ((KestSpan){0, 0})

typedef struct {
    const KestDecl *decl;
    const char *name;
    // Where this body allocates, if it does directly. Zero length when it
    // does not.
    KestSpan site;
    // Indices of the functions this one calls, and where each call is.
    uint32_t *callees;
    KestSpan *calls;
    uint32_t call_count;
    uint32_t call_capacity;
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

static int32_t find_function(Graph *graph, KestSpan name) {
    const char *text = span_text(graph, name);
    for (uint32_t i = 0; i < graph->count; i++) {
        if (strlen(graph->functions[i].name) == name.length &&
            memcmp(graph->functions[i].name, text, name.length) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
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
        if (function->site.length == 0) {
            function->site = expr->span;
        }
        function->allocates = true;
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
        if (callee->kind == KEST_EXPR_NAME &&
            find_function(graph, callee->span) < 0) {
            const char *text = span_text(graph, callee->span);
            bool allocating =
                (callee->span.length == 5 && memcmp(text, "store", 5) == 0) ||
                (callee->span.length == 3 && memcmp(text, "add", 3) == 0);
            if (allocating) {
                if (function->site.length == 0) {
                    function->site = expr->span;
                }
                function->allocates = true;
            }
        }
        // Building a struct is not a call and does not reach anything. A
        // dotted callee is an extern named for its host type.
        bool named = callee->kind == KEST_EXPR_NAME &&
                     (callee->type == NULL ||
                      callee->type->tag != KEST_T_STRUCT);
        if (named || callee->kind == KEST_EXPR_FIELD) {
            int32_t index = find_function(graph, callee->span);
            if (index >= 0) {
                record_call(graph, function, (uint32_t)index, expr->span);
            }
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
        walk_expr(graph, function, stmt->value);
        break;
    case KEST_STMT_IF:
        walk_expr(graph, function, stmt->branch.condition);
        walk_block(graph, function, &stmt->branch.then_body);
        if (stmt->branch.otherwise != NULL) {
            walk_stmt(graph, function, stmt->branch.otherwise);
        }
        break;
    case KEST_STMT_WHILE:
        walk_expr(graph, function, stmt->loop.condition);
        walk_block(graph, function, &stmt->loop.body);
        break;
    case KEST_STMT_FOR:
        walk_expr(graph, function, stmt->each.sequence);
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
    uint32_t count;
    KestSpan site;
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

        path->names[path->count++] = callee->name;
        if (callee->is_extern) {
            path->site = function->calls[i];
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

bool kest_check_contracts(KestProgram *program, const KestUnit *unit) {
    Graph graph = {0};
    graph.program = program;

    for (uint32_t i = 0; i < unit->count; i++) {
        if (unit->items[i]->kind == KEST_DECL_FN) {
            graph.count++;
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
    for (uint32_t i = 0; i < unit->count; i++) {
        const KestDecl *decl = unit->items[i];
        if (decl->kind != KEST_DECL_FN) {
            continue;
        }
        Function *function = &graph.functions[next++];
        function->decl = decl;
        // An extern is named for the host type it belongs to, so `Clock.now`
        // and `Timer.now` are two functions.
        KestSpan whole = decl->name;
        if (decl->function.receiver.length > 0) {
            whole.offset = decl->function.receiver.offset;
            whole.length = decl->name.offset + decl->name.length -
                           decl->function.receiver.offset;
        }
        function->name = kest_arena_strndup(
            program->arena, program->source->text + whole.offset, whole.length);
        function->promises = decl->function.no_alloc;
        function->is_extern = decl->function.is_extern;
        // A foreign body is not here to be read, so its promise is the only
        // thing there is to go on.
        function->allocates = function->is_extern && !function->promises;
        function->site = NO_SITE;
    }

    for (uint32_t i = 0; i < graph.count; i++) {
        Function *function = &graph.functions[i];
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
        if (!trace(&graph, i, &path)) {
            path.site = function->decl->name;
        }

        kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0401", path.site,
                       "this allocates, and `%s` promises `no.alloc`",
                       function->name);

        if (path.ends_in_extern) {
            kest_diags_suggest(program->diags,
                               "`%s` is declared to allocate", 
                               path.names[path.count - 1]);
        } else if (path.count > 0) {
            char through[512];
            size_t used = 0;
            for (uint32_t n = 0; n < path.count; n++) {
                used += (size_t)snprintf(through + used, sizeof(through) - used,
                                         "%s`%s`", n == 0 ? "" : " -> ",
                                         path.names[n]);
            }
            kest_diags_suggest(program->diags, "reached through %s", through);
        }
    }
    return true;
}
