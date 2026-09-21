#include "emitc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// How many arguments and how much of a frame this will write. A body wider
// than these is one the machine runs: what stops it is nothing in C, and a
// ceiling written down is a ceiling a reader can see, where a backend that
// wrote a function of nine hundred arguments would be found out by somebody
// else's compiler.
#define MOST_PARAMS 32

// Text being built. The arena is the caller's and the buffer is grown in
// place, because what this writes is one file made of one string per body and
// nothing frees a piece of it on its own.
typedef struct {
    char *bytes;
    size_t count;
    size_t room;
} Text;

// One body, as far as this backend got with it. A body it could not write
// keeps its name and why: that is what the file says at the top, and what a
// reader asking why something is slow reads first.
typedef struct {
    const char *symbol;
    Text wrote;
    // Which functions of the module it calls, so that a body calling one this
    // backend did not write can be found and left out as well. A call is a
    // call to a C function here; there is no dispatch to fall back through.
    uint32_t *calls;
    uint32_t call_count;
    uint32_t call_room;
    uint16_t params;
    uint16_t results;
    bool written;
    const char *why;
} Body;

struct KestEmitC {
    KestArena *arena;
    // Where what is read while one body is written goes: how deep the stack
    // is at each operation, and which of them a branch lands on. It is put
    // back between bodies rather than kept, because a program is as many of
    // these as it has bodies and one of them is alive at a time.
    KestArena *scratch;
    Body *bodies;
    uint32_t count;
    uint32_t room;
    // Whether anything reached for `kest_real_to_int` or `kest_left_over`,
    // which are the two answers this backend does not write out for itself.
    // What a number outside a width becomes and what is left over from
    // dividing two floats are the machine's answers and the folder's alike
    // (D668, D669): a third copy here would be a third answer the day one of
    // them moves, so the file calls the library's.
    bool wants_library;
    bool out_of_memory;
};

// Room for something that is growing. The arena makes the last thing it
// handed out bigger and answers nothing for anything else, which is every
// buffer here as soon as a second one is growing beside it: what this adds is
// the other half, which is a new one and a copy. A doubling, so the copy
// happens a logarithmic number of times rather than every time something is
// written.
static void *grow(KestArena *arena, void *last, size_t was, size_t want,
                  size_t align) {
    void *bigger = last == NULL ? NULL
                                : kest_arena_extend(arena, last, was, want);
    if (bigger != NULL) {
        return bigger;
    }
    bigger = kest_arena_alloc(arena, want, align);
    if (bigger == NULL) {
        return NULL;
    }
    if (last != NULL) {
        memcpy(bigger, last, was);
    }
    return bigger;
}

static void say(KestEmitC *c, Text *text, const char *format, ...)
    KEST_SAYS(3, 4);

static void say(KestEmitC *c, Text *text, const char *format, ...) {
    for (int round = 0; round < 2; round++) {
        va_list args;
        va_start(args, format);
        size_t left = text->room - text->count;
        // Nothing rather than nowhere plus nought: a buffer that has not been
        // taken yet is a null pointer, and a null pointer with an offset added
        // to it is undefined however small the offset -- which the build that
        // checks itself says out loud.
        char *end = text->bytes == NULL ? NULL : text->bytes + text->count;
        int wrote = vsnprintf(end, left, format, args);
        va_end(args);
        if (wrote < 0) {
            c->out_of_memory = true;
            return;
        }
        if ((size_t)wrote < left) {
            text->count += (size_t)wrote;
            return;
        }
        size_t want = text->room == 0 ? 512 : text->room * 2;
        while (want < text->count + (size_t)wrote + 1) {
            want *= 2;
        }
        char *grown = grow(c->arena, text->bytes, text->room, want, 1);
        if (grown == NULL) {
            c->out_of_memory = true;
            return;
        }
        text->bytes = grown;
        text->room = want;
    }
    // Grown to fit and still not fitting is the one way out of that loop that
    // is not a return, and what it leaves is half a line of C.
    c->out_of_memory = true;
}

// What one body is being written as, kept while it is written and let go with
// it. The depth of the operand stack at each operation is worked out before
// anything is written: an operand is a place in a C array here rather than
// somewhere a pointer has got to, so where each one sits has to be known at
// every operation, including the ones two ways of getting there arrive at.
typedef struct {
    KestEmitC *c;
    const KestIrBody *body;
    Body *into;
    uint32_t *depth;
    bool *known;
    bool *landed;
    uint32_t stack;
    uint32_t deepest;
    // Why this body is not being written, or NULL. The first reason is kept:
    // a body with two things in it this backend has no C for is one body.
    const char *why;
    // And the same reason with the numbers in it, where there are numbers
    // worth reading. It is copied into the file's own memory afterwards,
    // because what is written about a body is read long after the body is
    // gone.
    char said[96];
} Walk;

static void cannot(Walk *walk, const char *why) {
    if (walk->why == NULL) {
        walk->why = why;
    }
}

// How much of the stack an operation reads and leaves. The resolved form makes
// a value once and reads it once, innermost first, so what an operation reads
// is what is on top: this is that rule read off the operation rather than
// worked out again. `meet` is the one that does not follow it -- two ways of
// arriving at one value read two values and leave one, and only one of the two
// ever ran -- so it is answered here rather than counted.
static void moves(const KestIrBody *body, const KestIrOp *op, uint32_t *reads,
                  uint32_t *leaves) {
    uint32_t wide =
        op->dest == KEST_IR_NONE ? 0 : body->values[op->dest].slots;
    if (op->kind == KEST_IR_MEET) {
        *reads = wide;
        *leaves = wide;
        return;
    }
    uint32_t taken = 0;
    for (uint16_t a = 0; a < op->arg_count; a++) {
        taken += body->values[body->args[op->first_arg + a]].slots;
    }
    *reads = taken;
    *leaves = wide;
}

// One way of getting to an operation, with what it leaves behind it. Every
// operand is a named place in C here, so where a value sits has to be the same
// whichever way the program arrived -- which the machine does not need, an
// instruction working from the top of the stack wherever it is.
//
// A way that arrives with less than another way leaves is a way the program
// cannot take. It is the guard of the last arm of a `match`: the checker
// proved the arms cover every case, so the edge where none of them matched is
// one nothing reaches, and the machine would read a slot nothing wrote if it
// ever did. That is allowed where the arms meet and nowhere else, and what
// falls short is written as a stop rather than as a jump into a value that is
// not there.
static bool arrives(Walk *walk, uint32_t at, uint32_t depth) {
    if (walk->known[at] && walk->depth[at] != depth) {
        // And only where two ways of arriving at one value meet, which is the
        // one place the walk that wrote the body puts an edge nothing takes.
        // Anywhere else, two depths are this backend and that walk
        // disagreeing about what an operation does, which is worth saying
        // rather than working around.
        if (walk->body->ops[at].kind != KEST_IR_MEET) {
            snprintf(walk->said, sizeof(walk->said),
                     "operation %u is reached with %u and with %u on the "
                     "stack",
                     at, walk->depth[at], depth);
            cannot(walk, walk->said);
            return false;
        }
        // The deepest way there is what the operation is written for, and the
        // ways that fall short of it are written as stops.
        if (depth > walk->depth[at]) {
            walk->depth[at] = depth;
        }
        return true;
    }
    walk->known[at] = true;
    walk->depth[at] = depth;
    return true;
}

// Which operations a branch lands on, and how deep the stack is at each. A
// body whose two ways to one operation leave different amounts behind is one
// this backend refuses rather than guesses about: every operand is a named
// place in C, and two arms that disagree about where a value sits are two arms
// writing to two places.
static bool depths(Walk *walk) {
    const KestIrBody *body = walk->body;
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if ((op->kind == KEST_IR_GO || op->kind == KEST_IR_ASK ||
             op->kind == KEST_IR_NEXT || op->kind == KEST_IR_SEEK_FROM ||
             op->kind == KEST_IR_SEEK_NEXT) &&
            op->target < body->op_count) {
            walk->landed[op->target] = true;
        }
    }
    walk->known[0] = body->op_count > 0;
    for (uint32_t i = 0; i < body->op_count; i++) {
        if (!walk->known[i]) {
            continue;
        }
        const KestIrOp *op = &body->ops[i];
        uint32_t reads = 0;
        uint32_t leaves = 0;
        moves(body, op, &reads, &leaves);
        if (reads > walk->depth[i]) {
            cannot(walk, "an operation reading more than the body has made");
            return false;
        }
        uint32_t after = walk->depth[i] - reads + leaves;
        if (after > walk->deepest) {
            walk->deepest = after;
        }
        uint32_t next = i + 1;
        uint32_t lands = op->target;
        bool goes_on = op->kind != KEST_IR_GO && op->kind != KEST_IR_GIVE;
        bool branches = op->kind == KEST_IR_GO || op->kind == KEST_IR_ASK ||
                        op->kind == KEST_IR_NEXT;
        if (goes_on && next < body->op_count && !arrives(walk, next, after)) {
            return false;
        }
        if (branches && lands < body->op_count) {
            // A branch backwards lands where the walk has been, which is a
            // loop: what it leaves has to be what was there, or the stack
            // grows a little every time round.
            if (lands <= i && walk->depth[lands] != after) {
                snprintf(walk->said, sizeof(walk->said),
                         "a loop landing on operation %u with %u rather than "
                         "%u on the stack",
                         lands, after, walk->depth[lands]);
                cannot(walk, walk->said);
                return false;
            }
            if (!arrives(walk, lands, after)) {
                return false;
            }
        }
    }
    // A branch landing where nothing arrives from above is a label this cannot
    // write, because what the stack holds there was never worked out.
    for (uint32_t i = 0; i < body->op_count; i++) {
        if (walk->landed[i] && !walk->known[i]) {
            cannot(walk, "a branch landing where nothing reaches");
            return false;
        }
    }
    return true;
}

// Where an operand sits, written the way the file writes it. Small buffers
// rather than one string built up, because every line below names two or three
// of these and a shared one would name the last of them three times.
typedef char Where[24];

static void at_stack(Where into, uint32_t slot) {
    snprintf(into, sizeof(Where), "s[%u]", slot);
}

static void at_frame(Where into, uint32_t slot) {
    snprintf(into, sizeof(Where), "f[%u]", slot);
}

// What arithmetic is in C, by what it answers. Three `%s`: where the answer
// goes and the two it is worked out from, in that order, and the answer's
// place is the left operand's -- which is what makes a binary operation one
// line whatever it is.
//
// Every one of these is what `vm.c` runs, written the same way round: whole
// numbers wrap through unsigned because signed overflow in C is undefined
// (D667), and a narrow float is worked out at its own width and kept in a
// slot as a double.
static const char *binary_c(uint16_t kind, const KestType *type) {
    bool real = kest_is_float(type);
    bool narrow = kest_is_narrow(type);
    bool without_sign = kest_is_unsigned(type);
    switch (kind) {
    case KEST_IR_ADD:
        return real ? (narrow ? "%s.f = (double)((float)%s.f + (float)%s.f);"
                              : "%s.f = %s.f + %s.f;")
                    : "%s.i = (int64_t)((uint64_t)%s.i + (uint64_t)%s.i);";
    case KEST_IR_SUB:
        return real ? (narrow ? "%s.f = (double)((float)%s.f - (float)%s.f);"
                              : "%s.f = %s.f - %s.f;")
                    : "%s.i = (int64_t)((uint64_t)%s.i - (uint64_t)%s.i);";
    case KEST_IR_MUL:
        return real ? (narrow ? "%s.f = (double)((float)%s.f * (float)%s.f);"
                              : "%s.f = %s.f * %s.f;")
                    : "%s.i = (int64_t)((uint64_t)%s.i * (uint64_t)%s.i);";
    case KEST_IR_DIV:
        if (real) {
            return narrow ? "%s.f = (double)((float)%s.f / (float)%s.f);"
                          : "%s.f = %s.f / %s.f;";
        }
        return without_sign
                   ? "%s.i = (int64_t)((uint64_t)%s.i / (uint64_t)%s.i);"
                   : NULL;
    case KEST_IR_MOD:
        if (real) {
            return NULL;
        }
        return without_sign
                   ? "%s.i = (int64_t)((uint64_t)%s.i %% (uint64_t)%s.i);"
                   : NULL;
    case KEST_IR_AND:
        return "%s.i = %s.i & %s.i;";
    case KEST_IR_OR:
        return "%s.i = %s.i | %s.i;";
    case KEST_IR_XOR:
        return "%s.i = %s.i ^ %s.i;";
    case KEST_IR_LT:
        return real            ? "%s.i = (%s.f < %s.f);"
               : without_sign  ? "%s.i = ((uint64_t)%s.i < (uint64_t)%s.i);"
                               : "%s.i = (%s.i < %s.i);";
    case KEST_IR_LE:
        return real            ? "%s.i = (%s.f <= %s.f);"
               : without_sign  ? "%s.i = ((uint64_t)%s.i <= (uint64_t)%s.i);"
                               : "%s.i = (%s.i <= %s.i);";
    case KEST_IR_GT:
        return real            ? "%s.i = (%s.f > %s.f);"
               : without_sign  ? "%s.i = ((uint64_t)%s.i > (uint64_t)%s.i);"
                               : "%s.i = (%s.i > %s.i);";
    case KEST_IR_GE:
        return real            ? "%s.i = (%s.f >= %s.f);"
               : without_sign  ? "%s.i = ((uint64_t)%s.i >= (uint64_t)%s.i);"
                               : "%s.i = (%s.i >= %s.i);";
    // Equality asks nothing about a sign: the same bits are the same bits
    // either way, which is what `lower` reads out of its own table.
    case KEST_IR_EQ:
        return real ? "%s.i = (%s.f == %s.f);" : "%s.i = (%s.i == %s.i);";
    case KEST_IR_NE:
        return real ? "%s.i = (%s.f != %s.f);" : "%s.i = (%s.i != %s.i);";
    default:
        return NULL;
    }
}

// The cut to a declared width, which is how every whole number is kept in a
// slot. The same six `kest_narrow_to` makes, written as the casts they are so
// that the host's compiler can see through them.
static const char *narrow_c(uint16_t scalar) {
    switch (scalar) {
    case KEST_L_I8:
        return "%s.i = (int64_t)(int8_t)%s.i;";
    case KEST_L_I16:
        return "%s.i = (int64_t)(int16_t)%s.i;";
    case KEST_L_I32:
        return "%s.i = (int64_t)(int32_t)%s.i;";
    case KEST_L_BOOL:
    case KEST_L_U8:
        return "%s.i = (int64_t)(uint8_t)%s.i;";
    case KEST_L_U16:
        return "%s.i = (int64_t)(uint16_t)%s.i;";
    case KEST_L_U32:
        return "%s.i = (int64_t)(uint32_t)%s.i;";
    default:
        return NULL;
    }
}

static void write_const(Walk *walk, const KestIrOp *op) {
    const KestIrBody *body = walk->body;
    uint32_t first = op->imm[0];
    uint32_t count = op->imm[1];
    if ((size_t)first + count > body->constant_count) {
        cannot(walk, "a value the body has not got");
        return;
    }
    for (uint32_t k = 0; k < count; k++) {
        KestValue value = body->constants[first + k];
        uint8_t class = body->constant_classes[first + k];
        Where into;
        at_stack(into, walk->stack + k);
        if (kest_slot_of(class) == KEST_S_REAL) {
            // Written as hexadecimal, which is the one spelling of a double
            // that reads back as the bits it was written from. What has no
            // such spelling -- an infinity, or what is not a number -- is a
            // body this backend leaves alone rather than one it writes a
            // number for that means something else.
            if (value.real != value.real ||
                value.real - value.real != 0.0) {
                cannot(walk, "a number with no spelling in C");
                return;
            }
            say(walk->c, &walk->into->wrote, "    %s.f = %a;\n", into,
                value.real);
            continue;
        }
        if (kest_slot_of(class) != KEST_S_INTEGER) {
            cannot(walk, "a value that is not a number");
            return;
        }
        say(walk->c, &walk->into->wrote,
            "    %s.i = (int64_t)UINT64_C(0x%016llx);\n", into,
            (unsigned long long)(uint64_t)value.integer);
    }
}

// Where a branch goes: the operation it lands on, or nowhere when this way
// there leaves less than the operation is written for. The second is the edge
// no program takes, which `arrives` explains.
static void write_branch(Walk *walk, uint32_t target, uint32_t leaving) {
    if (target < walk->body->op_count && walk->known[target] &&
        walk->depth[target] > leaving) {
        say(walk->c, &walk->into->wrote,
            "k_stopped(\"no arm of this answered\");\n");
        return;
    }
    say(walk->c, &walk->into->wrote, "goto L%u;\n", target);
}

// One operation, as the C it does. `walk->stack` is where the top of the
// operand stack is before it and is moved by it, which is the whole of the
// bookkeeping: every operand is a place in one array with a number worked out
// while compiling, so the host's compiler sees plain locals rather than a
// stack it has to follow.
static void write_op(Walk *walk, uint32_t index, const KestIrOp *op) {
    const KestIrBody *body = walk->body;
    Text *out = &walk->into->wrote;
    KestEmitC *c = walk->c;
    uint32_t reads = 0;
    uint32_t leaves = 0;
    moves(body, op, &reads, &leaves);
    uint32_t base = walk->stack - reads;
    Where first;
    Where second;
    Where third;
    switch ((KestIrKind)op->kind) {
    case KEST_IR_CONST:
        write_const(walk, op);
        break;
    case KEST_IR_TRUE:
    case KEST_IR_FALSE:
        at_stack(first, base);
        say(c, out, "    %s.i = %d;\n", first,
            op->kind == KEST_IR_TRUE ? 1 : 0);
        break;
    case KEST_IR_LOAD: {
        const KestIrPlace *place = &body->places[op->place];
        if (place->kind != KEST_IR_PLACE_SLOT) {
            cannot(walk, "a place that is not a run of the frame");
            break;
        }
        for (uint16_t k = 0; k < place->slots; k++) {
            at_stack(first, base + k);
            at_frame(second, (uint32_t)(place->slot + k));
            say(c, out, "    %s = %s;\n", first, second);
        }
        break;
    }
    case KEST_IR_PUT: {
        const KestIrPlace *place = &body->places[op->place];
        if (place->kind != KEST_IR_PLACE_SLOT) {
            cannot(walk, "a place that is not a run of the frame");
            break;
        }
        if (place->slots != reads) {
            cannot(walk, "a write of a different width than what it writes");
            break;
        }
        for (uint16_t k = 0; k < place->slots; k++) {
            at_frame(first, (uint32_t)(place->slot + k));
            at_stack(second, base + k);
            say(c, out, "    %s = %s;\n", first, second);
        }
        break;
    }
    // A value out of its parts is already its parts, laid out where they were
    // made, and two ways of arriving at one value are one value. Neither is
    // anything here, which is the same answer `lower` gives.
    case KEST_IR_MAKE:
    case KEST_IR_MEET:
    case KEST_IR_NOTHING:
        break;
    case KEST_IR_PART: {
        uint32_t offset = op->imm[0];
        uint32_t wide = op->imm[1];
        uint32_t whole = op->imm[2];
        if (whole != reads || wide != leaves ||
            (size_t)offset + wide > whole) {
            cannot(walk, "a field that is not part of what it is a field of");
            break;
        }
        for (uint32_t k = 0; k < wide && offset > 0; k++) {
            at_stack(first, base + k);
            at_stack(second, base + offset + k);
            say(c, out, "    %s = %s;\n", first, second);
        }
        break;
    }
    case KEST_IR_TURN: {
        uint32_t count = op->imm[0];
        if (count != reads || count != leaves || count == 0) {
            cannot(walk, "a rotation of something other than what it reads");
            break;
        }
        // The last slot is the tag and belongs first, so the run is rolled by
        // one rather than reversed. The machine does it with a move; here it
        // is the same move written out, because the host's compiler can see
        // through assignments and cannot see through `memmove`.
        at_stack(first, base + count - 1);
        say(c, out, "    {\n        KV turned = %s;\n", first);
        for (uint32_t k = count - 1; k > 0; k--) {
            at_stack(first, base + k);
            at_stack(second, base + k - 1);
            say(c, out, "        %s = %s;\n", first, second);
        }
        at_stack(first, base);
        say(c, out, "        %s = turned;\n    }\n", first);
        break;
    }
    case KEST_IR_DROP:
        break;
    case KEST_IR_ADD:
    case KEST_IR_SUB:
    case KEST_IR_MUL:
    case KEST_IR_AND:
    case KEST_IR_OR:
    case KEST_IR_XOR:
    case KEST_IR_LT:
    case KEST_IR_LE:
    case KEST_IR_GT:
    case KEST_IR_GE:
    case KEST_IR_EQ:
    case KEST_IR_NE: {
        if (kest_is_a_run(op->type) || (op->type != NULL &&
                                   op->type->tag == KEST_T_TEXT)) {
            cannot(walk, "an answer about something wider than a number");
            break;
        }
        const char *how = binary_c(op->kind, op->type);
        if (how == NULL || reads != 2 || leaves != 1) {
            cannot(walk, "arithmetic with no C");
            break;
        }
        at_stack(first, base);
        at_stack(second, base);
        at_stack(third, base + 1);
        say(c, out, "    ");
        say(c, out, how, first, second, third);
        say(c, out, "\n");
        break;
    }
    case KEST_IR_DIV:
    case KEST_IR_MOD: {
        if (reads != 2 || leaves != 1) {
            cannot(walk, "arithmetic with no C");
            break;
        }
        at_stack(first, base);
        at_stack(second, base);
        at_stack(third, base + 1);
        if (kest_is_float(op->type)) {
            if (op->kind == KEST_IR_DIV) {
                say(c, out, "    ");
                say(c, out, binary_c(op->kind, op->type), first, second,
                    third);
                say(c, out, "\n");
                break;
            }
            // What is left over from dividing two floats is the library's
            // answer rather than one written again here. See D970.
            c->wants_library = true;
            say(c, out, "    %s.f = %skest_left_over(%s.f, %s.f);\n", first,
                kest_is_narrow(op->type) ? "(double)(float)" : "", second,
                third);
            break;
        }
        // Dividing by nought stops the program where the machine stops it,
        // and the one pair whose quotient does not fit answers what wrapping
        // says rather than what the host's machine traps on. See D667.
        say(c, out, "    if (%s.i == 0) {\n", third);
        say(c, out, "        k_stopped(\"division by zero\");\n    }\n");
        if (kest_is_unsigned(op->type)) {
            say(c, out, "    ");
            say(c, out, binary_c(op->kind, op->type), first, second, third);
            say(c, out, "\n");
            break;
        }
        say(c, out,
            "    %s.i = (%s.i == INT64_MIN && %s.i == -1) ? %s : (%s.i %s "
            "%s.i);\n",
            first, second, third,
            op->kind == KEST_IR_DIV ? "INT64_MIN" : "0", second,
            op->kind == KEST_IR_DIV ? "/" : "%", third);
        break;
    }
    case KEST_IR_NEG:
    case KEST_IR_FLIP: {
        if (reads != 1 || leaves != 1) {
            cannot(walk, "arithmetic with no C");
            break;
        }
        at_stack(first, base);
        at_stack(second, base);
        if (op->kind == KEST_IR_FLIP) {
            say(c, out, "    %s.i = ~%s.i;\n", first, second);
            break;
        }
        if (kest_is_float(op->type)) {
            say(c, out, "    %s.f = %s-%s.f;\n", first,
                kest_is_narrow(op->type) ? "(double)(float)" : "", second);
            break;
        }
        // The smallest number negated is itself, which is what wrapping says
        // and what negating it signed would leave undefined.
        say(c, out, "    %s.i = (int64_t)(0 - (uint64_t)%s.i);\n", first,
            second);
        break;
    }
    case KEST_IR_SHL:
    case KEST_IR_SHR: {
        if (reads != 2 || leaves != 1) {
            cannot(walk, "arithmetic with no C");
            break;
        }
        at_stack(first, base);
        at_stack(second, base);
        at_stack(third, base + 1);
        say(c, out, "    if (%s.i < 0) {\n", third);
        say(c, out, "        k_stopped(\"a shift is not a count\");\n    }\n");
        // A count past the width of a slot has no meaning in C, so it is
        // answered here rather than left to the machine: everything shifts
        // out, and a signed number keeps its sign.
        if (op->kind == KEST_IR_SHL) {
            say(c, out,
                "    %s.i = %s.i >= 64 ? 0 : (int64_t)((uint64_t)%s.i << "
                "%s.i);\n",
                first, third, second, third);
            break;
        }
        if (kest_is_unsigned(op->type)) {
            say(c, out,
                "    %s.i = %s.i >= 64 ? 0 : (int64_t)((uint64_t)%s.i >> "
                "%s.i);\n",
                first, third, second, third);
            break;
        }
        say(c, out,
            "    %s.i = %s.i >= 64 ? (%s.i < 0 ? -1 : 0) : (%s.i >> %s.i);\n",
            first, third, second, second, third);
        break;
    }
    case KEST_IR_NARROW: {
        const char *how = narrow_c(op->imm[0]);
        if (reads != 1 || leaves != 1) {
            cannot(walk, "a cut of something other than a number");
            break;
        }
        if (how == NULL) {
            // A width a slot already holds, which is no cut at all.
            break;
        }
        at_stack(first, base);
        at_stack(second, base);
        say(c, out, "    ");
        say(c, out, how, first, second);
        say(c, out, "\n");
        break;
    }
    case KEST_IR_TO_FLOAT:
        at_stack(first, base);
        at_stack(second, base);
        say(c, out, "    %s.f = (double)%s%s.i;\n", first,
            kest_is_unsigned(op->type) ? "(uint64_t)" : "", second);
        break;
    case KEST_IR_TO_WHOLE:
        // Where a number outside the width stops is the library's answer, for
        // the reason the one above it is. See D669.
        c->wants_library = true;
        at_stack(first, base);
        at_stack(second, base);
        say(c, out, "    %s.i = kest_real_to_int(%u, %s.f);\n", first,
            (unsigned)op->imm[0], second);
        break;
    case KEST_IR_TO_F32:
        at_stack(first, base);
        at_stack(second, base);
        say(c, out, "    %s.f = (double)(float)%s.f;\n", first, second);
        break;
    case KEST_IR_NOT:
        at_stack(first, base);
        at_stack(second, base);
        say(c, out, "    %s.i = !%s.i;\n", first, second);
        break;
    case KEST_IR_CALL: {
        uint32_t which = op->imm[0];
        if (op->imm[1] != reads) {
            cannot(walk, "a call handing over something other than what it "
                         "read");
            break;
        }
        if (walk->into->call_count == walk->into->call_room) {
            uint32_t room = walk->into->call_room == 0
                                ? 8
                                : walk->into->call_room * 2;
            uint32_t *grown =
                grow(c->arena, walk->into->calls,
                     sizeof(uint32_t) * walk->into->call_room,
                     sizeof(uint32_t) * room, sizeof(uint32_t));
            if (grown == NULL) {
                c->out_of_memory = true;
                break;
            }
            walk->into->calls = grown;
            walk->into->call_room = room;
        }
        walk->into->calls[walk->into->call_count++] = which;
        say(c, out, "    ");
        if (leaves == 1) {
            at_stack(first, base);
            say(c, out, "%s = ", first);
        }
        say(c, out, "kf_%u(", which);
        if (leaves > 1) {
            at_stack(first, base);
            say(c, out, "&%s%s", first, reads > 0 ? ", " : "");
        }
        for (uint32_t k = 0; k < reads; k++) {
            at_stack(first, base + k);
            say(c, out, "%s%s", k > 0 ? ", " : "", first);
        }
        say(c, out, ");\n");
        break;
    }
    case KEST_IR_GO:
        say(c, out, "    ");
        write_branch(walk, op->target, base + leaves);
        break;
    case KEST_IR_ASK:
        if (reads != leaves + 1) {
            cannot(walk, "a branch reading something other than an answer");
            break;
        }
        at_stack(first, walk->stack - 1);
        say(c, out, "    if (%s%s.i) {\n        ", op->imm[1] != 0 ? "" : "!",
            first);
        write_branch(walk, op->target, base + leaves);
        say(c, out, "    }\n");
        break;
    case KEST_IR_NEXT: {
        // A walk's step: count on, decide, and go round again or leave. The
        // machine has one instruction for it and this is the same three
        // things, which is what makes a loop here a loop the host's compiler
        // recognises.
        at_frame(first, op->imm[0]);
        at_frame(second, op->imm[1]);
        say(c, out, "    %s.i += 1;\n", first);
        say(c, out, "    if (%s%s.i %s %s%s.i) {\n        ",
            kest_is_unsigned(op->type) ? "(uint64_t)" : "", first,
            kest_is_unsigned(op->type) ? "<" : "<",
            kest_is_unsigned(op->type) ? "(uint64_t)" : "", second);
        write_branch(walk, op->target, base + leaves);
        say(c, out, "    }\n");
        break;
    }
    case KEST_IR_GIVE: {
        uint32_t count = op->imm[0];
        if (count != reads || count != walk->into->results) {
            cannot(walk, "giving back something other than what it gives");
            break;
        }
        if (count == 0) {
            say(c, out, "    return;\n");
            break;
        }
        if (count == 1) {
            at_stack(first, base);
            say(c, out, "    return %s;\n", first);
            break;
        }
        for (uint32_t k = 0; k < count; k++) {
            at_stack(first, base + k);
            say(c, out, "    out[%u] = %s;\n", k, first);
        }
        say(c, out, "    return;\n");
        break;
    }
    default:
        // Everything else: the heap, text, stores, the host, working memory,
        // and every place that is not a run of the frame. A `default` here is
        // not a list left incomplete -- it is what this backend is, which is
        // a growing half of a language whose other half runs everything. An
        // operation added and forgotten here is a body the machine runs.
        cannot(walk, kest_ir_word((KestIrKind)op->kind));
        break;
    }
    (void)index;
    walk->stack = base + leaves;
}

// What a body is declared as. A result of one slot comes back as the value it
// is, a wider one is written where the caller says, and nothing else about a
// call crosses: what the machine keeps in a frame the host's compiler keeps
// wherever it likes.
static void write_head(KestEmitC *c, Text *into, const Body *body,
                       uint32_t which) {
    say(c, into, "static %s kf_%u(", body->results == 1 ? "KV" : "void",
        which);
    if (body->results > 1) {
        say(c, into, "KV *out%s", body->params > 0 ? ", " : "");
    }
    for (uint16_t p = 0; p < body->params; p++) {
        say(c, into, "%sKV a%u", p > 0 ? ", " : "", (unsigned)p);
    }
    if (body->params == 0 && body->results <= 1) {
        say(c, into, "void");
    }
    say(c, into, ")");
}

// A body as this backend reads it, for whoever is growing it. What stops a
// body is said in the file it writes; what a body is made of is not, and the
// two questions a reader has when something was left out are "which
// operation" and "what was on the stack". Read once, because a compiler that
// asked the environment per body could change its mind half way through a
// program. It is the same door `KEST_IRSAY` is, one level down.
static bool saying(void) {
    static int asked = -1;
    if (asked < 0) {
        asked = getenv("KEST_CSAY") == NULL ? 0 : 1;
    }
    return asked != 0;
}

static void say_body(const KestIrBody *body) {
    fprintf(stderr, "c %s: %u slots, %u deep, %u in, %u out\n",
            body->symbol == NULL ? "(no name)" : body->symbol,
            body->slot_count, body->stack_needed, body->param_slots,
            body->result_slots);
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        uint32_t reads = 0;
        uint32_t leaves = 0;
        moves(body, op, &reads, &leaves);
        fprintf(stderr,
                "  %3u %-12s reads %u leaves %u  carries %u %u %u  lands %u\n",
                i, kest_ir_word((KestIrKind)op->kind), reads, leaves,
                op->imm[0], op->imm[1], op->imm[2], op->target);
    }
}

KestEmitC *kest_emitc_new(KestArena *arena) {
    KestEmitC *c = KEST_ARENA_NEW(arena, KestEmitC);
    if (c == NULL) {
        return NULL;
    }
    memset(c, 0, sizeof(*c));
    c->arena = arena;
    c->scratch = kest_arena_new();
    if (c->scratch == NULL) {
        return NULL;
    }
    return c;
}

bool kest_emitc_body(void *writing, const KestIrBody *body) {
    KestEmitC *c = writing;
    if (c->count == c->room) {
        uint32_t room = c->room == 0 ? 16 : c->room * 2;
        Body *grown = grow(c->arena, c->bodies, sizeof(Body) * c->room,
                           sizeof(Body) * room, sizeof(void *));
        if (grown == NULL) {
            c->out_of_memory = true;
            return false;
        }
        c->bodies = grown;
        c->room = room;
    }
    Body *into = &c->bodies[c->count++];
    memset(into, 0, sizeof(*into));
    into->params = body->param_slots;
    into->results = body->result_slots;
    // The name is copied: a body lives in an arena of its own and is let go
    // before the file is written.
    if (body->symbol != NULL) {
        into->symbol =
            kest_arena_strndup(c->arena, body->symbol, strlen(body->symbol));
        if (into->symbol == NULL) {
            c->out_of_memory = true;
            return false;
        }
    }
    if (saying()) {
        say_body(body);
    }
    if (body->param_slots > MOST_PARAMS) {
        into->why = "more arguments than this writes";
        return true;
    }

    KestMark before = kest_arena_mark(c->arena);
    kest_arena_reset(c->scratch);
    uint32_t many = body->op_count == 0 ? 1 : body->op_count;
    Walk walk;
    memset(&walk, 0, sizeof(walk));
    walk.c = c;
    walk.body = body;
    walk.into = into;
    walk.depth = KEST_ARENA_ARRAY(c->scratch, uint32_t, many);
    walk.known = KEST_ARENA_ARRAY(c->scratch, bool, many);
    walk.landed = KEST_ARENA_ARRAY(c->scratch, bool, many);
    if (walk.depth == NULL || walk.known == NULL || walk.landed == NULL) {
        c->out_of_memory = true;
        return false;
    }
    memset(walk.depth, 0, sizeof(uint32_t) * many);
    memset(walk.known, 0, sizeof(bool) * many);
    memset(walk.landed, 0, sizeof(bool) * many);

    if (depths(&walk)) {
        write_head(c, &into->wrote, into, c->count - 1);
        say(c, &into->wrote, " {\n");
        if (body->slot_count > 0) {
            say(c, &into->wrote, "    KV f[%u];\n", (unsigned)body->slot_count);
        }
        if (walk.deepest > 0) {
            say(c, &into->wrote, "    KV s[%u];\n", walk.deepest);
        }
        for (uint16_t p = 0; p < body->param_slots; p++) {
            say(c, &into->wrote, "    f[%u] = a%u;\n", (unsigned)p,
                (unsigned)p);
        }
        // Said out loud rather than left to whether the body happens to read
        // them: a frame nothing reads is a warning in somebody else's build,
        // and a warning in a generated file is noise a reader learns to skip.
        if (body->slot_count > 0) {
            say(c, &into->wrote, "    (void)f;\n");
        }
        if (walk.deepest > 0) {
            say(c, &into->wrote, "    (void)s;\n");
        }
        for (uint32_t i = 0; i < body->op_count && walk.why == NULL; i++) {
            if (!walk.known[i]) {
                continue;
            }
            if (walk.landed[i]) {
                say(c, &into->wrote, "L%u:;\n", i);
            }
            walk.stack = walk.depth[i];
            write_op(&walk, i, &body->ops[i]);
            // And the same where an operation falls into the next with less
            // than the next is written for, which is the other end of what
            // `arrives` allows.
            if (i + 1 < body->op_count && walk.known[i + 1] &&
                walk.depth[i + 1] > walk.stack) {
                say(c, &into->wrote,
                    "    k_stopped(\"no arm of this answered\");\n");
            }
        }
        say(c, &into->wrote, "}\n\n");
    }
    if (c->out_of_memory) {
        return false;
    }
    if (walk.why != NULL) {
        into->wrote.bytes = NULL;
        into->wrote.count = 0;
        into->wrote.room = 0;
        into->calls = NULL;
        into->call_count = 0;
        into->call_room = 0;
        kest_arena_rewind(c->arena, before);
        // After the rewind, because what was written about this body is
        // written where what was written of it was.
        into->why = walk.why == walk.said
                        ? kest_arena_strndup(c->arena, walk.said,
                                             strlen(walk.said))
                        : walk.why;
        if (into->why == NULL) {
            c->out_of_memory = true;
            return false;
        }
        return true;
    }
    into->written = true;
    return true;
}

// A body that calls one this backend did not write is one it cannot write
// either: there is no dispatch here to fall back through, so the call has
// nowhere to go. Followed until nothing moves, because the caller of a caller
// is in the same position.
static void settle(KestEmitC *c) {
    bool moved = true;
    while (moved) {
        moved = false;
        for (uint32_t i = 0; i < c->count; i++) {
            Body *body = &c->bodies[i];
            if (!body->written) {
                continue;
            }
            for (uint32_t k = 0; k < body->call_count; k++) {
                uint32_t which = body->calls[k];
                if (which < c->count && c->bodies[which].written) {
                    continue;
                }
                body->written = false;
                body->why = "it calls a body this does not write";
                moved = true;
                break;
            }
        }
    }
}

const char *kest_emitc_done(KestEmitC *c, const char *entry) {
    // What was read while the bodies were written is read no further.
    kest_arena_free(c->scratch);
    c->scratch = NULL;
    settle(c);
    Text file = {NULL, 0, 0};
    say(c, &file,
        "// Written by `kest emit --c`. What this means is the program it was\n"
        "// written from: edit that and write this again.\n"
        "//\n"
        "// A slot is eight bytes whatever is in it, the same union the\n"
        "// machine has, because this and the machine are one program: a body\n"
        "// neither could write is a body the other runs.\n"
        "#include <stdint.h>\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "\n"
        "typedef union {\n"
        "    int64_t i;\n"
        "    double f;\n"
        "    const char *t;\n"
        "    void *o;\n"
        "} KV;\n"
        "\n"
        "static void k_stopped(const char *why) {\n"
        "    fprintf(stderr, \"stopped: %%s\\n\", why);\n"
        "    exit(1);\n"
        "}\n\n");
    if (c->wants_library) {
        say(c, &file,
            "// The two answers this file does not work out for itself: what\n"
            "// a number outside a width becomes, and what is left over from\n"
            "// dividing two floats. They are the machine's answers and the\n"
            "// folder's alike, so this calls them rather than being a third\n"
            "// copy. Link against `libkest.a`.\n"
            "int64_t kest_real_to_int(uint16_t scalar, double value);\n"
            "double kest_left_over(double left, double right);\n\n");
    }

    uint32_t written = 0;
    for (uint32_t i = 0; i < c->count; i++) {
        if (c->bodies[i].written) {
            written++;
            continue;
        }
        say(c, &file, "// not written: %s -- %s\n",
            c->bodies[i].symbol == NULL ? "(no name)" : c->bodies[i].symbol,
            c->bodies[i].why == NULL ? "no reason" : c->bodies[i].why);
    }
    say(c, &file, "// %u of %u bodies written\n\n", written, c->count);

    for (uint32_t i = 0; i < c->count; i++) {
        if (!c->bodies[i].written) {
            continue;
        }
        write_head(c, &file, &c->bodies[i], i);
        say(c, &file, ";\n");
    }
    say(c, &file, "\n");
    for (uint32_t i = 0; i < c->count; i++) {
        if (!c->bodies[i].written) {
            continue;
        }
        say(c, &file, "// %s\n",
            c->bodies[i].symbol == NULL ? "(no name)" : c->bodies[i].symbol);
        say(c, &file, "%s", c->bodies[i].wrote.bytes);
    }

    // And the way in. A program whose entry is a body this did not write is a
    // file with no `main` in it: what it holds is still worth reading, and
    // nothing here pretends it is a program.
    for (uint32_t i = 0; entry != NULL && i < c->count; i++) {
        const Body *body = &c->bodies[i];
        if (!body->written || body->symbol == NULL ||
            strcmp(body->symbol, entry) != 0 || body->params != 0 ||
            body->results > 1) {
            continue;
        }
        // The status a run answers with, which is the command line's rule
        // rather than the language's: a file this backend wrote is a host of
        // the same program, and a host that answered something else would be
        // two programs rather than one compiled two ways.
        say(c, &file, "int main(void) {\n");
        if (body->results == 1) {
            say(c, &file,
                "    KV answer = kf_%u();\n"
                "    if (answer.i < 0 || answer.i > 255) {\n"
                "        fprintf(stderr, \"`main` answered %%lld, and an exit "
                "status carries 0 to 255\\n\",\n"
                "                (long long)answer.i);\n"
                "        return 1;\n"
                "    }\n"
                "    return (int)answer.i;\n}\n",
                i);
        } else {
            say(c, &file, "    kf_%u();\n    return 0;\n}\n", i);
        }
        break;
    }
    if (c->out_of_memory || file.bytes == NULL) {
        return NULL;
    }
    return file.bytes;
}
