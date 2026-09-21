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
    // What the module says about itself, which is two things this backend
    // asks: how a value is laid out where memory is shared, and what a body
    // it is about to call promised. Both are known before any body is
    // compiled, so a call forward is answered the same as a call back. See
    // D1095.
    const KestModule *module;
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
    // Whether this body may hold a handle in a local, which is whether
    // nothing it does can reach the heap. Worked out once before anything is
    // written.
    bool no_heap;
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

// Whether nothing this body does can reach the heap. It is what says a handle
// may sit in a C local: the collector walks the machine's stack for roots and
// a local is not on it, so a body holding a handle across anything that can
// allocate is a body whose handle can go out from under it. A body that
// cannot allocate at all cannot be in the middle of a collection, so there is
// nothing to see. What a call reaches is read off the callee's declaration,
// which the module carries before any body is compiled. See D1095.
static bool reaches_no_heap(const KestEmitC *c, const KestIrBody *body) {
    for (uint32_t i = 0; i < body->op_count; i++) {
        const KestIrOp *op = &body->ops[i];
        if ((op->effects & (KEST_IR_EFFECT_ALLOCATES | KEST_IR_EFFECT_HOST |
                            KEST_IR_EFFECT_MOVES)) != 0) {
            return false;
        }
        if (op->kind == KEST_IR_CALL_VALUE || op->kind == KEST_IR_CALL_HOST) {
            return false;
        }
        if (op->kind != KEST_IR_CALL) {
            continue;
        }
        if (c->module == NULL || op->imm[0] >= c->module->count ||
            !c->module->functions[op->imm[0]]->no_alloc) {
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

// One scalar moved between memory and slots: the same switch the machine runs
// over a piece, written out at the width the piece is, which a C compiler
// turns into one load or one store.
static void move_one(Walk *walk, uint8_t kind, uint32_t slot, uint32_t byte,
                     bool reading) {
    KestEmitC *c = walk->c;
    Text *out = &walk->into->wrote;
    Where held;
    Where beside;
    at_stack(held, slot);
    const char *width = NULL;
    bool real = false;
    switch (kind) {
    case KEST_L_TEXT:
        // Two slots: what it is made of and how many bytes that is. The bytes
        // are the heap's and are carried rather than copied.
        at_stack(beside, slot + 1);
        if (reading) {
            say(c, out,
                "        memcpy(&%s, at + %u, 8);\n"
                "        {\n            uint64_t many;\n"
                "            memcpy(&many, at + %u, 8);\n"
                "            %s.integer = (int64_t)many;\n        }\n",
                held, byte, byte + 8, beside);
            return;
        }
        say(c, out,
            "        memcpy(at + %u, &%s, 8);\n"
            "        {\n            uint64_t many = (uint64_t)%s.integer;\n"
            "            memcpy(at + %u, &many, 8);\n        }\n",
            byte, held, beside, byte + 8);
        return;
    case KEST_L_I8:
        width = "int8_t";
        break;
    case KEST_L_I16:
        width = "int16_t";
        break;
    case KEST_L_I32:
        width = "int32_t";
        break;
    case KEST_L_U8:
    case KEST_L_BOOL:
    case KEST_L_HELD:
        width = "uint8_t";
        break;
    case KEST_L_U16:
        width = "uint16_t";
        break;
    case KEST_L_U32:
        width = "uint32_t";
        break;
    case KEST_L_F32:
        width = "float";
        real = true;
        break;
    case KEST_L_F64:
        width = "double";
        real = true;
        break;
    default:
        break;
    }
    if (width == NULL) {
        // A whole slot either way, which is what the machine moves for
        // everything it has no narrower name for.
        if (reading) {
            say(c, out, "        memcpy(&%s, at + %u, 8);\n", held, byte);
        } else {
            say(c, out, "        memcpy(at + %u, &%s, 8);\n", byte, held);
        }
        return;
    }
    if (reading) {
        say(c, out,
            "        {\n            %s piece;\n"
            "            memcpy(&piece, at + %u, sizeof piece);\n"
            "            %s.%s = piece;\n        }\n",
            width, byte, held, real ? "real" : "integer");
        return;
    }
    say(c, out,
        "        {\n            %s piece = (%s)%s.%s;\n"
        "            memcpy(at + %u, &piece, sizeof piece);\n        }\n",
        width, width, held, real ? "real" : "integer", byte);
}

// A whole value moved between memory and slots, by walking the type rather
// than the flat list of pieces a layout carries. The machine has both walks
// and picks between them -- a run of pieces for anything with no tag in it,
// and the type itself for anything with one, because which slots a payload
// fills is what the tag says (D710). Here there is one walk, because a walk
// done while compiling costs nothing at either end and a tag is a `switch`
// the host's compiler can see through. What the machine does with a loop over
// a layout for every element is a run of moves here, and that loop is a third
// of `bench/rules.kest`. See D1028 and D1096.
//
// Answers how many slots it moved. `where` is where in the source this is,
// for the one thing reading a value can refuse: a tag that names no case.
static uint16_t move_value(Walk *walk, const KestType *type, uint32_t slot,
                           uint32_t byte, bool reading, uint32_t where) {
    KestEmitC *c = walk->c;
    Text *out = &walk->into->wrote;
    if (type == NULL) {
        move_one(walk, KEST_L_WORD, slot, byte, reading);
        return 1;
    }
    if (type->tag == KEST_T_STRUCT) {
        uint16_t used = 0;
        for (uint32_t i = 0; i < type->member_count; i++) {
            used = (uint16_t)(used +
                              move_value(walk, type->members[i].type,
                                         slot + used,
                                         byte + type->members[i].byte_offset,
                                         reading, where));
        }
        return used;
    }
    if (type->tag == KEST_T_FIXED) {
        uint16_t used = 0;
        for (uint32_t i = 0; i < type->count; i++) {
            used = (uint16_t)(used +
                              move_value(walk, type->element, slot + used,
                                         byte + i * type->element->byte_size,
                                         reading, where));
        }
        return used;
    }
    if (type->tag == KEST_T_OPTIONAL) {
        uint16_t used = move_value(walk, type->element, slot, byte, reading,
                                   where);
        move_one(walk, KEST_L_HELD, slot + used,
                 byte + type->element->byte_size, reading);
        return (uint16_t)(used + 1);
    }
    if (type->tag != KEST_T_ENUM) {
        move_one(walk, kest_scalar_of(type), slot, byte, reading);
        return type->tag == KEST_T_TEXT ? 2 : 1;
    }
    Where tag;
    at_stack(tag, slot);
    if (!reading) {
        // What the case does not carry is written as nought, because a tag
        // says which reading the bytes beside it have and a case written over
        // a wider one would otherwise leave the wider one's fields under the
        // new tag. See D711.
        say(c, out,
            "        memset(at + %u, 0, %u);\n"
            "        {\n            int32_t tag = (int32_t)%s.integer;\n"
            "            memcpy(at + %u, &tag, 4);\n"
            "            switch (tag) {\n",
            byte, (unsigned)type->byte_size, tag, byte);
    } else {
        say(c, out,
            "        {\n            int32_t tag;\n"
            "            memcpy(&tag, at + %u, 4);\n"
            "            %s.integer = tag;\n",
            byte, tag);
        for (uint16_t piece = 1; piece < type->slots; piece++) {
            Where empty;
            at_stack(empty, slot + piece);
            say(c, out, "            %s.integer = 0;\n", empty);
        }
        say(c, out, "            switch (tag) {\n");
    }
    for (uint32_t which = 0; which < type->case_count; which++) {
        const KestVariantType *variant = &type->cases[which];
        say(c, out, "            case %u:\n", which);
        for (uint32_t piece = 0; piece < variant->payload_count; piece++) {
            move_value(walk, variant->payload[piece],
                       slot + variant->offsets[piece],
                       byte + variant->byte_offsets[piece], reading, where);
        }
        say(c, out, "                break;\n");
    }
    if (!reading) {
        // Writing one does not refuse a tag with no case behind it: the tag
        // goes down and nothing else does, which is what the machine writes.
        say(c, out,
            "            default:\n                break;\n"
            "            }\n        }\n");
        return type->slots;
    }
    const char *written = kest_type_written(type);
    say(c, out,
        "            default: {\n                char said[96];\n"
        "                snprintf(said, sizeof said,\n"
        "                         \"`%s` here holds tag %%lld and has no such "
        "case\",\n"
        "                         (long long)tag);\n"
        "                return kest_native_stopped(rt, %u, \"K0651\", "
        "said);\n"
        "            }\n            }\n        }\n",
        written == NULL ? "a value with a tag in it" : written, where);
    return type->slots;
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
        return real ? (narrow ? "%s.real = (double)((float)%s.real + (float)%s.real);"
                              : "%s.real = %s.real + %s.real;")
                    : "%s.integer = (int64_t)((uint64_t)%s.integer + (uint64_t)%s.integer);";
    case KEST_IR_SUB:
        return real ? (narrow ? "%s.real = (double)((float)%s.real - (float)%s.real);"
                              : "%s.real = %s.real - %s.real;")
                    : "%s.integer = (int64_t)((uint64_t)%s.integer - (uint64_t)%s.integer);";
    case KEST_IR_MUL:
        return real ? (narrow ? "%s.real = (double)((float)%s.real * (float)%s.real);"
                              : "%s.real = %s.real * %s.real;")
                    : "%s.integer = (int64_t)((uint64_t)%s.integer * (uint64_t)%s.integer);";
    case KEST_IR_DIV:
        if (real) {
            return narrow ? "%s.real = (double)((float)%s.real / (float)%s.real);"
                          : "%s.real = %s.real / %s.real;";
        }
        return without_sign
                   ? "%s.integer = (int64_t)((uint64_t)%s.integer / (uint64_t)%s.integer);"
                   : NULL;
    case KEST_IR_MOD:
        if (real) {
            return NULL;
        }
        return without_sign
                   ? "%s.integer = (int64_t)((uint64_t)%s.integer %% (uint64_t)%s.integer);"
                   : NULL;
    case KEST_IR_AND:
        return "%s.integer = %s.integer & %s.integer;";
    case KEST_IR_OR:
        return "%s.integer = %s.integer | %s.integer;";
    case KEST_IR_XOR:
        return "%s.integer = %s.integer ^ %s.integer;";
    case KEST_IR_LT:
        return real            ? "%s.integer = (%s.real < %s.real);"
               : without_sign  ? "%s.integer = ((uint64_t)%s.integer < (uint64_t)%s.integer);"
                               : "%s.integer = (%s.integer < %s.integer);";
    case KEST_IR_LE:
        return real            ? "%s.integer = (%s.real <= %s.real);"
               : without_sign  ? "%s.integer = ((uint64_t)%s.integer <= (uint64_t)%s.integer);"
                               : "%s.integer = (%s.integer <= %s.integer);";
    case KEST_IR_GT:
        return real            ? "%s.integer = (%s.real > %s.real);"
               : without_sign  ? "%s.integer = ((uint64_t)%s.integer > (uint64_t)%s.integer);"
                               : "%s.integer = (%s.integer > %s.integer);";
    case KEST_IR_GE:
        return real            ? "%s.integer = (%s.real >= %s.real);"
               : without_sign  ? "%s.integer = ((uint64_t)%s.integer >= (uint64_t)%s.integer);"
                               : "%s.integer = (%s.integer >= %s.integer);";
    // Equality asks nothing about a sign: the same bits are the same bits
    // either way, which is what `lower` reads out of its own table.
    case KEST_IR_EQ:
        return real ? "%s.integer = (%s.real == %s.real);" : "%s.integer = (%s.integer == %s.integer);";
    case KEST_IR_NE:
        return real ? "%s.integer = (%s.real != %s.real);" : "%s.integer = (%s.integer != %s.integer);";
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
        return "%s.integer = (int64_t)(int8_t)%s.integer;";
    case KEST_L_I16:
        return "%s.integer = (int64_t)(int16_t)%s.integer;";
    case KEST_L_I32:
        return "%s.integer = (int64_t)(int32_t)%s.integer;";
    case KEST_L_BOOL:
    case KEST_L_U8:
        return "%s.integer = (int64_t)(uint8_t)%s.integer;";
    case KEST_L_U16:
        return "%s.integer = (int64_t)(uint16_t)%s.integer;";
    case KEST_L_U32:
        return "%s.integer = (int64_t)(uint32_t)%s.integer;";
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
        // What the slot is, which is what a chunk says about its own
        // constants rather than what a layout says about a type: three
        // things, because what a reader of a chunk needs is to tell a number
        // from a float from the bytes of a piece of text.
        uint8_t class = body->constant_classes[first + k];
        Where into;
        at_stack(into, walk->stack + k);
        // The bytes of a piece of text are where they are in the process that
        // compiled it, and a number written down here is read in another
        // process: a body holding one is a body the machine runs until this
        // knows how to write text that outlives the compiler.
        if (class == KEST_CONST_TEXT) {
            cannot(walk, "a piece of text written where it was compiled");
            return;
        }
        if (class == KEST_CONST_FLOAT) {
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
            say(walk->c, &walk->into->wrote, "    %s.real = %a;\n", into,
                value.real);
            continue;
        }
        if (class != KEST_CONST_INT) {
            cannot(walk, "a value this backend has no spelling for");
            return;
        }
        say(walk->c, &walk->into->wrote,
            "    %s.integer = (int64_t)UINT64_C(0x%016llx);\n", into,
            (unsigned long long)(uint64_t)value.integer);
    }
}

// One of an array, read into slots or written out of them. The two things
// the machine asks before it touches one -- that the handle is an array and
// that the index is inside it -- are a call, because they are the same two
// questions however wide an element is; the moving is written out, because
// which piece sits where is known while compiling. See D1095.
static bool write_elem(Walk *walk, const KestIrOp *op,
                       const KestIrPlace *place, uint32_t handle,
                       uint32_t value, bool reading) {
    KestEmitC *c = walk->c;
    Text *out = &walk->into->wrote;
    if (!walk->no_heap) {
        cannot(walk, "a handle held where this body can reach the heap");
        return false;
    }
    if (c->module == NULL || place->layout >= c->module->layout_count) {
        cannot(walk, "an element of a shape this module has not laid out");
        return false;
    }
    const KestLayout *layout = &c->module->layouts[place->layout];
    if (layout->type == NULL || layout->slots != place->slots) {
        cannot(walk, "an element read at a width the layout does not have");
        return false;
    }
    Where held;
    Where index;
    at_stack(held, handle);
    at_stack(index, handle + 1);
    say(c, out,
        "    {\n        unsigned char *at = kest_elem_at(rt, %s, %s.integer, "
        "%u, %u);\n"
        "        if (at == NULL) {\n            return false;\n        }\n",
        held, index, (unsigned)place->offset, op->span.offset);
    uint16_t moved = move_value(walk, layout->type, value, 0, reading,
                                op->span.offset);
    say(c, out, "    }\n");
    if (moved != layout->slots) {
        cannot(walk, "an element whose type and layout say different widths");
        return false;
    }
    return true;
}

// A field read through an address that was worked out before it. The address
// is the program's to hold -- an element of an array, and nothing moves under
// it while it is held, which is what D931 and D996 make true -- so there is
// nothing to check here and nothing to call.
static bool write_at(Walk *walk, const KestIrPlace *place, uint32_t value,
                     uint32_t address) {
    KestEmitC *c = walk->c;
    if (!walk->no_heap) {
        cannot(walk, "a handle held where this body can reach the heap");
        return false;
    }
    if (c->module == NULL || place->layout >= c->module->layout_count) {
        cannot(walk, "a field of a shape this module has not laid out");
        return false;
    }
    const KestLayout *layout = &c->module->layouts[place->layout];
    if (layout->type == NULL || layout->slots != place->slots) {
        cannot(walk, "a field read at a width the layout does not have");
        return false;
    }
    Where held;
    at_stack(held, address);
    say(c, &walk->into->wrote,
        "    {\n        unsigned char *at = (unsigned char *)%s.object + %u;\n",
        held, (unsigned)place->offset);
    uint16_t moved = move_value(walk, layout->type, value, 0, true,
                                walk->body->ops[0].span.offset);
    say(c, &walk->into->wrote, "    }\n");
    if (moved != layout->slots) {
        cannot(walk, "a field whose type and layout say different widths");
        return false;
    }
    return true;
}

// Where a branch goes: the operation it lands on, or nowhere when this way
// there leaves less than the operation is written for. The second is the edge
// no program takes, which `arrives` explains.
static void write_branch(Walk *walk, uint32_t target, uint32_t leaving,
                         uint32_t where) {
    if (target < walk->body->op_count && walk->known[target] &&
        walk->depth[target] > leaving) {
        // Said in the machine's own words and with the machine's own code for
        // it: what got here is this project being wrong about its own
        // program, which is what `K0655` is.
        say(walk->c, &walk->into->wrote,
            "return kest_native_stopped(rt, %u, \"K0655\",\n"
            "            \"no way to this operation left a value for it\");\n",
            where);
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
        say(c, out, "    %s.integer = %d;\n", first,
            op->kind == KEST_IR_TRUE ? 1 : 0);
        break;
    case KEST_IR_LOAD: {
        const KestIrPlace *place = &body->places[op->place];
        if (place->kind == KEST_IR_PLACE_ELEM) {
            // The handle and the index are the two slots under what this
            // leaves, and whether they are read away is which of the two
            // element reads it is: one that consumes them is an index, and
            // one that does not is a place a write is coming to.
            uint32_t handle = reads == 0 ? walk->stack - 2 : base;
            if (!write_elem(walk, op, place, handle, base, true)) {
                break;
            }
            break;
        }
        if (place->kind == KEST_IR_PLACE_AT) {
            // An address worked out before this, and how far into what it
            // points at the field sits. The machine has an instruction that
            // does this and the element read before it in one (D1044); here
            // the two are two lines the host's compiler puts together itself.
            if (!write_at(walk, place, base, reads == 0 ? walk->stack - 1
                                                        : base)) {
                break;
            }
            break;
        }
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
        if (place->kind == KEST_IR_PLACE_ELEM) {
            // The handle, the index, and then what is being written, which is
            // what the machine pops in that order.
            if (reads < 3) {
                cannot(walk, "a write of an element with nothing to write");
                break;
            }
            write_elem(walk, op, place, base, base + 2, false);
            break;
        }
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
            say(c, out, "    %s.real = %skest_left_over(%s.real, %s.real);\n", first,
                kest_is_narrow(op->type) ? "(double)(float)" : "", second,
                third);
            break;
        }
        // Dividing by nought stops the program where the machine stops it,
        // and the one pair whose quotient does not fit answers what wrapping
        // says rather than what the host's machine traps on. See D667.
        say(c, out, "    if (%s.integer == 0) {\n", third);
        say(c, out,
            "        return kest_native_stopped(rt, %u, \"K0601\",\n"
            "            \"division by zero\");\n    }\n",
            op->span.offset);
        if (kest_is_unsigned(op->type)) {
            say(c, out, "    ");
            say(c, out, binary_c(op->kind, op->type), first, second, third);
            say(c, out, "\n");
            break;
        }
        say(c, out,
            "    %s.integer = (%s.integer == INT64_MIN && %s.integer == -1) ? %s : (%s.integer %s "
            "%s.integer);\n",
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
            say(c, out, "    %s.integer = ~%s.integer;\n", first, second);
            break;
        }
        if (kest_is_float(op->type)) {
            say(c, out, "    %s.real = %s-%s.real;\n", first,
                kest_is_narrow(op->type) ? "(double)(float)" : "", second);
            break;
        }
        // The smallest number negated is itself, which is what wrapping says
        // and what negating it signed would leave undefined.
        say(c, out, "    %s.integer = (int64_t)(0 - (uint64_t)%s.integer);\n", first,
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
        // The machine says how far it was asked to shift, in those words,
        // and so does this: two engines that refuse the same program with
        // two sentences are two languages, and what holds them to one is a
        // check that reads both.
        say(c, out, "    if (%s.integer < 0) {\n", third);
        say(c, out,
            "        char said[64];\n"
            "        snprintf(said, sizeof said, \"a shift of %%lld is not "
            "a count\",\n"
            "                 (long long)%s.integer);\n"
            "        return kest_native_stopped(rt, %u, \"K0604\", said);\n"
            "    }\n",
            third, op->span.offset);
        // A count past the width of a slot has no meaning in C, so it is
        // answered here rather than left to the machine: everything shifts
        // out, and a signed number keeps its sign.
        if (op->kind == KEST_IR_SHL) {
            say(c, out,
                "    %s.integer = %s.integer >= 64 ? 0 : (int64_t)((uint64_t)%s.integer << "
                "%s.integer);\n",
                first, third, second, third);
            break;
        }
        if (kest_is_unsigned(op->type)) {
            say(c, out,
                "    %s.integer = %s.integer >= 64 ? 0 : (int64_t)((uint64_t)%s.integer >> "
                "%s.integer);\n",
                first, third, second, third);
            break;
        }
        say(c, out,
            "    %s.integer = %s.integer >= 64 ? (%s.integer < 0 ? -1 : 0) : (%s.integer >> %s.integer);\n",
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
        say(c, out, "    %s.real = (double)%s%s.integer;\n", first,
            kest_is_unsigned(op->type) ? "(uint64_t)" : "", second);
        break;
    case KEST_IR_TO_WHOLE:
        // Where a number outside the width stops is the library's answer, for
        // the reason the one above it is. See D669.
        c->wants_library = true;
        at_stack(first, base);
        at_stack(second, base);
        say(c, out, "    %s.integer = kest_real_to_int(%u, %s.real);\n", first,
            (unsigned)op->imm[0], second);
        break;
    case KEST_IR_TO_F32:
        at_stack(first, base);
        at_stack(second, base);
        say(c, out, "    %s.real = (double)(float)%s.real;\n", first, second);
        break;
    case KEST_IR_NOT:
        at_stack(first, base);
        at_stack(second, base);
        say(c, out, "    %s.integer = !%s.integer;\n", first, second);
        break;
    case KEST_IR_ADDR: {
        const KestIrPlace *place = &body->places[op->place];
        if (place->kind != KEST_IR_PLACE_ELEM) {
            cannot(walk, "the address of a place this does not take one of");
            break;
        }
        if (!walk->no_heap) {
            cannot(walk, "a handle held where this body can reach the heap");
            break;
        }
        if (reads != 2 || leaves != 1) {
            cannot(walk, "an address of something other than one of a run");
            break;
        }
        at_stack(first, base);
        at_stack(second, base + 1);
        say(c, out,
            "    %s.object = kest_elem_at(rt, %s, %s.integer, 0, %u);\n"
            "    if (%s.object == NULL) {\n        return false;\n    }\n",
            first, first, second, op->span.offset, first);
        break;
    }
    case KEST_IR_LEN: {
        if (reads != 1 || leaves != 1) {
            cannot(walk, "a length of something other than one thing");
            break;
        }
        if (!walk->no_heap) {
            cannot(walk, "a handle held where this body can reach the heap");
            break;
        }
        at_stack(first, base);
        say(c, out, "    if (!kest_elem_count(rt, %s, %u, &%s.integer)) {\n"
                    "        return false;\n    }\n",
            first, op->span.offset, first);
        break;
    }
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
        // What it answers is whether it ran. A body that stopped has already
        // said so through the machine, so the caller gives back what it gave
        // back and nothing here writes a second message about it.
        at_stack(first, base);
        say(c, out, "    if (!kf_%u(rt, %s%s", which, leaves > 0 ? "&" : "",
            leaves > 0 ? first : "NULL");
        for (uint32_t k = 0; k < reads; k++) {
            at_stack(second, base + k);
            say(c, out, ", %s", second);
        }
        say(c, out, ")) {\n        return false;\n    }\n");
        break;
    }
    case KEST_IR_GO:
        say(c, out, "    ");
        write_branch(walk, op->target, base + leaves, op->span.offset);
        break;
    case KEST_IR_ASK:
        if (reads != leaves + 1) {
            cannot(walk, "a branch reading something other than an answer");
            break;
        }
        at_stack(first, walk->stack - 1);
        say(c, out, "    if (%s%s.integer) {\n        ", op->imm[1] != 0 ? "" : "!",
            first);
        write_branch(walk, op->target, base + leaves, op->span.offset);
        say(c, out, "    }\n");
        break;
    case KEST_IR_NEXT: {
        // A walk's step: count on, decide, and go round again or leave. The
        // machine has one instruction for it and this is the same three
        // things, which is what makes a loop here a loop the host's compiler
        // recognises.
        at_frame(first, op->imm[0]);
        at_frame(second, op->imm[1]);
        say(c, out, "    %s.integer += 1;\n", first);
        say(c, out, "    if (%s%s.integer %s %s%s.integer) {\n        ",
            kest_is_unsigned(op->type) ? "(uint64_t)" : "", first,
            kest_is_unsigned(op->type) ? "<" : "<",
            kest_is_unsigned(op->type) ? "(uint64_t)" : "", second);
        write_branch(walk, op->target, base + leaves, op->span.offset);
        say(c, out, "    }\n");
        break;
    }
    case KEST_IR_GIVE: {
        uint32_t count = op->imm[0];
        if (count != reads || count != walk->into->results) {
            cannot(walk, "giving back something other than what it gives");
            break;
        }
        for (uint32_t k = 0; k < count; k++) {
            at_stack(first, base + k);
            say(c, out, "    out[%u] = %s;\n", k, first);
        }
        say(c, out, "    return true;\n");
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

// What a body is declared as. It answers whether it ran rather than what it
// worked out, because a body can stop -- dividing by nought, shifting by a
// count that is not one -- and what it worked out goes where the caller says.
// The machine comes with it because a body that stops says so through the
// machine, which is what makes a refusal from compiled code read like a
// refusal from the instructions. See D1094.
static void write_head(KestEmitC *c, Text *into, const Body *body,
                       uint32_t which) {
    say(c, into, "static bool kf_%u(KestRuntime *rt, KV *out", which);
    for (uint16_t p = 0; p < body->params; p++) {
        say(c, into, ", KV a%u", (unsigned)p);
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

KestEmitC *kest_emitc_new(KestArena *arena, const KestModule *module) {
    KestEmitC *c = KEST_ARENA_NEW(arena, KestEmitC);
    if (c == NULL) {
        return NULL;
    }
    memset(c, 0, sizeof(*c));
    c->arena = arena;
    c->module = module;
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

    walk.no_heap = reaches_no_heap(c, body);
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
        say(c, &into->wrote, "    (void)rt;\n    (void)out;\n");
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
                    "    return kest_native_stopped(rt, %u, \"K0655\",\n"
                    "        \"no way to this operation left a value for "
                    "it\");\n",
                    body->ops[i].span.offset);
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

const char *kest_emitc_done(KestEmitC *c, const char *entry,
                            const char *from) {
    // What was read while the bodies were written is read no further.
    kest_arena_free(c->scratch);
    c->scratch = NULL;
    settle(c);
    Text file = {NULL, 0, 0};
    say(c, &file,
        "// Written by `kest emit --c`. What this means is the program it was\n"
        "// written from: edit that and write this again.\n"
        "//\n"
        "// It is a host of that program as well as a translation of it. A\n"
        "// body this backend had no C for is named below with the reason and\n"
        "// is a body the machine runs, so what this holds is half a program\n"
        "// and the machine holds the other half. See D1093 and D1094.\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#include \"kest.h\"\n"
        "\n"
        "// A slot, under the name the machine's own backend gives it.\n"
        "typedef KestValue KV;\n"
        "\n"
        "// The doors a file this backend wrote calls, and the only ones that\n"
        "// are not in the public header: one binds a body to the chunk it\n"
        "// was written from, the other says what a body says when it stops.\n"
        "// Written out here rather than included, because what this includes\n"
        "// is what a host includes. Link against `libkest.a`.\n"
        "bool kest_native_at(KestRuntime *runtime, uint32_t index,\n"
        "                    const char *symbol,\n"
        "                    bool (*body)(KestRuntime *, KestValue *,\n"
        "                                 uint16_t *));\n"
        "bool kest_native_stopped(KestRuntime *runtime, uint32_t offset,\n"
        "                         const char *code, const char *message);\n"
        "unsigned char *kest_elem_at(KestRuntime *runtime, KestValue handle,\n"
        "                            int64_t index, uint16_t offset,\n"
        "                            uint32_t where);\n"
        "bool kest_elem_count(KestRuntime *runtime, KestValue handle,\n"
        "                     uint32_t where, int64_t *into);\n\n");
    if (c->wants_library) {
        say(c, &file,
            "// And the two answers this file does not work out for itself:\n"
            "// what a number outside a width becomes, and what is left over\n"
            "// from dividing two floats. They are the machine's answers and\n"
            "// the folder's alike, so this calls them rather than being a\n"
            "// third copy.\n"
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

    // How many bodies this file holds, and how many times the machine
    // entered each of them. A check that cannot tell whether any of this ran
    // is a check that passes when none of it does, and two engines that
    // answer alike answer alike when one of them never started. It is one
    // increment at a crossing that already costs a call.
    uint32_t bound_count = 0;
    for (uint32_t i = 0; i < c->count; i++) {
        if (c->bodies[i].written && c->bodies[i].symbol != NULL) {
            bound_count++;
        }
    }
    say(c, &file, "\nstatic uint64_t kest_entered[%u];\n\n",
        bound_count == 0 ? 1 : bound_count);

    // What the machine calls, which is not what the C calls. A body is
    // written here as the C it is, taking its arguments as values and
    // answering through a pointer; a call from the machine hands over a frame
    // and is handed back how many slots came of it. One of these a body, so
    // the shape the machine needs costs the C nothing.
    uint32_t bound_so_far = 0;
    for (uint32_t i = 0; i < c->count; i++) {
        const Body *body = &c->bodies[i];
        if (!body->written) {
            continue;
        }
        say(c, &file,
            "static bool kn_%u(KestRuntime *rt, KestValue *frame,\n"
            "                 uint16_t *gave) {\n    kest_entered[%u]++;\n"
            "    if (!kf_%u(rt, %s",
            i, bound_so_far++, i, body->results > 0 ? "frame" : "NULL");
        for (uint16_t p = 0; p < body->params; p++) {
            say(c, &file, ", frame[%u]", (unsigned)p);
        }
        say(c, &file,
            ")) {\n        return false;\n    }\n    *gave = %u;\n"
            "    return true;\n}\n",
            (unsigned)body->results);
    }

    // And which chunk each of them belongs to. The number is where the body
    // is in the module, which is what this file names them by; the name is
    // held against what is there, so a file written from another version of
    // the program refuses rather than putting one body's C under another
    // body's name.
    say(c, &file,
        "\nstatic bool bound(KestRuntime *rt) {\n    return true");
    for (uint32_t i = 0; i < c->count; i++) {
        if (!c->bodies[i].written || c->bodies[i].symbol == NULL) {
            continue;
        }
        say(c, &file, " &&\n           kest_native_at(rt, %u, \"%s\", kn_%u)",
            i, c->bodies[i].symbol, i);
    }
    say(c, &file, ";\n}\n");

    // And the way in: this file is a host of the program it was written from,
    // because half of that program may still be the machine's to run. It
    // builds the same program, binds what it wrote, and calls `main` -- and
    // what answers `main` is whichever of the two engines holds it.
    const Body *way_in = NULL;
    for (uint32_t i = 0; entry != NULL && i < c->count; i++) {
        if (c->bodies[i].symbol != NULL &&
            strcmp(c->bodies[i].symbol, entry) == 0) {
            way_in = &c->bodies[i];
            break;
        }
    }
    // The one door a program needs to say anything, provided here so that a
    // program that writes a line is a program this can run. Everything else a
    // host provides -- a clock, a file, what the process was started with,
    // whatever an engine offers -- is the host's to bind and is not bound
    // here: a file this backend wrote is half a program rather than an engine.
    // See D1094.
    say(c, &file,
        "\n// What `std.io` asks the host for. Written by its length rather\n"
        "// than to a nought, because a piece of text cut out of the middle "
        "of\n// another does not end in one.\n"
        "static void wrote_it(KestValue *frame, KestRuntime *runtime,\n"
        "                     void *context) {\n"
        "    (void)runtime;\n"
        "    (void)context;\n"
        "    uint32_t length = 0;\n"
        "    const char *bytes = kest_text_bytes(frame, &length);\n"
        "    if (bytes != NULL && length > 0) {\n"
        "        fwrite(bytes, 1, length, stdout);\n"
        "    }\n"
        "}\n");
    say(c, &file,
        "\n// How much of a run was this file's, for whoever asks: a program\n"
        "// that stopped is a program half of which may still have run, so it\n"
        "// is said on the way out either way.\n"
        "static void said_how_much(const char *saying) {\n"
        "    if (saying == NULL) {\n        return;\n    }\n"
        "    uint32_t ran = 0;\n"
        "    uint64_t times = 0;\n"
        "    for (size_t i = 0; i < sizeof(kest_entered) /\n"
        "                           sizeof(kest_entered[0]); i++) {\n"
        "        times += kest_entered[i];\n"
        "        if (kest_entered[i] > 0) {\n            ran++;\n        }\n"
        "    }\n"
        "    fprintf(stderr, \"natives: %u written, %%u entered, %%llu "
        "time(s)\\n\",\n"
        "            ran, (unsigned long long)times);\n"
        "}\n"
        "\nint main(int argc, char **argv) {\n",
        bound_count);
    say(c, &file,
        "    // The program this was written from, or another copy of it "
        "named\n"
        "    // on the command line. The C is half of a program and this is "
        "the\n"
        "    // other half; a file that has moved on since is refused by the\n"
        "    // binding rather than run.\n"
        "    const char *path = argc > 1 ? argv[1] : \"%s\";\n"
        "    // Where the library is, the way anything that is not this\n"
        "    // project's own command line finds it: the command line looks\n"
        "    // beside itself and then where it was installed, and a host has\n"
        "    // neither of those to go on.\n"
        "    KestBuild *build = kest_build(path, getenv(\"KEST_LIB\"), "
        "stderr,\n                                  KEST_FORM_TEXT, 0);\n"
        "    if (build == NULL) {\n"
        "        fprintf(stderr, \"`%%s` is not a program this can read\\n\","
        "\n                path);\n"
        "        return 1;\n"
        "    }\n"
        "    KestHost *host = kest_host_new();\n"

        "    if (host == NULL ||\n"
        "        !kest_host_bind(host, \"Io.write\", wrote_it, NULL)) {\n"
        "        fprintf(stderr, \"there is no room for a host\\n\");\n"
        "        kest_build_free(build);\n"
        "        return 1;\n"
        "    }\n"

        "    KestRuntime *rt = kest_start(build, host, NULL);\n"
        "    kest_host_free(host);\n"
        "    if (rt == NULL) {\n"
        "        kest_build_report(build, stderr, KEST_FORM_TEXT);\n"
        "        kest_build_free(build);\n"
        "        return 1;\n"
        "    }\n"
        "    if (!bound(rt)) {\n"
        "        fprintf(stderr, \"this C was written from another `%%s`\\n\","
        "\n                path);\n"
        "        kest_runtime_free(rt);\n"
        "        kest_build_free(build);\n"
        "        return 1;\n"
        "    }\n"
        "    // How much of this run was this file's, when somebody asks.\n"
        "    const char *saying = getenv(\"KEST_NATIVES\");\n"
        "    int32_t entry = kest_entry(rt, KEST_MAIN);\n"
        "    KestValue frame[8] = {{0}};\n"
        "    if (entry < 0 || !kest_call(rt, entry, frame,\n"
        "                                sizeof(frame) / sizeof(frame[0]))) "
        "{\n"
        "        kest_report(rt, stderr, KEST_FORM_TEXT);\n"
        "        said_how_much(saying);\n"
        "        kest_runtime_free(rt);\n"
        "        kest_build_free(build);\n"
        "        return 1;\n"
        "    }\n"
        "    said_how_much(saying);\n",
        from == NULL ? "" : from);
    // The status a run answers with, which is the command line's rule rather
    // than the language's: a file this backend wrote is a host of the same
    // program, and a host that answered something else would be two programs
    // rather than one compiled two ways.
    if (way_in != NULL && way_in->results == 1) {
        say(c, &file,
            "    int64_t answer = frame[0].integer;\n"
            "    kest_runtime_free(rt);\n"
            "    kest_build_free(build);\n"
            "    if (answer < 0 || answer > 255) {\n"
            "        fprintf(stderr,\n"
            "                \"`main` answered %%lld, and an exit status "
            "carries 0 to 255\\n\",\n"
            "                (long long)answer);\n"
            "        return 1;\n"
            "    }\n"
            "    return (int)answer;\n}\n");
    } else {
        say(c, &file,
            "    kest_runtime_free(rt);\n"
            "    kest_build_free(build);\n"
            "    return 0;\n}\n");
    }
    if (c->out_of_memory || file.bytes == NULL) {
        return NULL;
    }
    return file.bytes;
}
