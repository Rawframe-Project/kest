#include "vm.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

// What a host gets when it says nothing.
#define STACK_SLOTS 65536
#define MAX_FRAMES 1024

// An array is a length and a run of elements laid out the way the host lays
// them out: an array of `f32` is four bytes an element. The block is separate
// from the header so that it can one day be the host's own. What frees it is
// not decided; see D012.
// What a handle is. A host holds these as opaque values and can hand one back
// where another was wanted, which nothing at the boundary can see: the machine
// carries no types and D046 says why. So the handle says what it is.
#define KEST_IS_ARRAY 0x4b415252u
#define KEST_IS_STORE 0x4b53544fu

// Both headers begin with it, so which one a handle is can be read without
// knowing which one it was meant to be.
#define KEST_HANDLE_IS(handle, tag)                                          \
    ((handle) != NULL && *(const uint32_t *)(handle) == (tag))

typedef struct {
    uint32_t what;
    uint32_t length;
    uint32_t capacity;
    uint16_t stride;
    // Lent by the host, which means the block is not ours to move and the
    // array is not ours to grow.
    bool borrowed;
    unsigned char *bytes;
} Array;

// Memory to stack and back. Everything goes through memcpy, because a
// borrowed block is aligned the way its owner aligned it and not the way this
// machine would like.
static void unpack(KestValue *out, const KestLayout *layout,
                   const unsigned char *from);
static void pack(unsigned char *to, const KestLayout *layout,
                 const KestValue *from);

// A value moved by what it is rather than by a list of pieces, which is what
// a tagged union needs: the tag says which types the slots after it hold.
static uint16_t unpack_typed(KestValue *out, const KestType *type,
                             const unsigned char *from);
static uint16_t pack_typed(unsigned char *to, const KestType *type,
                           const KestValue *from);

static uint16_t move_scalar(KestValue *out, const KestType *type,
                            const unsigned char *from, bool reading,
                            unsigned char *to) {
    KestPiece piece = {0, kest_scalar_of(type)};
    KestLayout one = {&piece, 1, 0, 0, NULL, false};
    if (reading) {
        unpack(out, &one, from);
    } else {
        pack(to, &one, out);
    }
    return 1;
}

static uint16_t unpack_typed(KestValue *out, const KestType *type,
                             const unsigned char *from) {
    if (type == NULL) {
        memcpy(&out[0], from, sizeof(KestValue));
        return 1;
    }
    if (type->tag == KEST_T_STRUCT) {
        uint16_t used = 0;
        for (uint32_t i = 0; i < type->member_count; i++) {
            used += unpack_typed(out + used, type->members[i].type,
                                 from + type->members[i].byte_offset);
        }
        return used;
    }
    if (type->tag == KEST_T_FIXED) {
        uint16_t used = 0;
        for (uint32_t i = 0; i < type->count; i++) {
            used += unpack_typed(out + used, type->element,
                                 from + i * type->element->byte_size);
        }
        return used;
    }
    if (type->tag == KEST_T_OPTIONAL) {
        uint16_t used = unpack_typed(out, type->element, from);
        uint8_t held;
        memcpy(&held, from + type->element->byte_size, 1);
        out[used].integer = held;
        return (uint16_t)(used + 1);
    }
    if (type->tag == KEST_T_ENUM) {
        int32_t tag;
        memcpy(&tag, from, 4);
        out[0].integer = tag;
        for (uint16_t s = 1; s < type->slots; s++) {
            out[s].integer = 0;
        }
        if (tag >= 0 && (uint32_t)tag < type->case_count) {
            const KestVariantType *variant = &type->cases[tag];
            for (uint32_t p = 0; p < variant->payload_count; p++) {
                unpack_typed(out + variant->offsets[p], variant->payload[p],
                             from + variant->byte_offsets[p]);
            }
        }
        return type->slots;
    }
    return move_scalar(out, type, from, true, NULL);
}

static uint16_t pack_typed(unsigned char *to, const KestType *type,
                           const KestValue *from) {
    if (type == NULL) {
        memcpy(to, &from[0], sizeof(KestValue));
        return 1;
    }
    if (type->tag == KEST_T_STRUCT) {
        uint16_t used = 0;
        for (uint32_t i = 0; i < type->member_count; i++) {
            used += pack_typed(to + type->members[i].byte_offset,
                               type->members[i].type, from + used);
        }
        return used;
    }
    if (type->tag == KEST_T_FIXED) {
        uint16_t used = 0;
        for (uint32_t i = 0; i < type->count; i++) {
            used += pack_typed(to + i * type->element->byte_size,
                               type->element, from + used);
        }
        return used;
    }
    if (type->tag == KEST_T_OPTIONAL) {
        uint16_t used = pack_typed(to, type->element, from);
        uint8_t held = (uint8_t)from[used].integer;
        memcpy(to + type->element->byte_size, &held, 1);
        return (uint16_t)(used + 1);
    }
    if (type->tag == KEST_T_ENUM) {
        int32_t tag = (int32_t)from[0].integer;
        memcpy(to, &tag, 4);
        if (tag >= 0 && (uint32_t)tag < type->case_count) {
            const KestVariantType *variant = &type->cases[tag];
            for (uint32_t p = 0; p < variant->payload_count; p++) {
                pack_typed(to + variant->byte_offsets[p], variant->payload[p],
                           from + variant->offsets[p]);
            }
        }
        return type->slots;
    }
    return move_scalar((KestValue *)from, type, NULL, false, to);
}

static void unpack(KestValue *out, const KestLayout *layout,
                   const unsigned char *from) {
    if (layout->tagged) {
        unpack_typed(out, layout->type, from);
        return;
    }
    for (uint16_t i = 0; i < layout->count; i++) {
        const unsigned char *at = from + layout->pieces[i].offset;
        switch (layout->pieces[i].kind) {
        case KEST_L_I8: {
            int8_t v;
            memcpy(&v, at, 1);
            out[i].integer = v;
            break;
        }
        case KEST_L_I16: {
            int16_t v;
            memcpy(&v, at, 2);
            out[i].integer = v;
            break;
        }
        case KEST_L_I32: {
            int32_t v;
            memcpy(&v, at, 4);
            out[i].integer = v;
            break;
        }
        case KEST_L_U8: {
            uint8_t v;
            memcpy(&v, at, 1);
            out[i].integer = v;
            break;
        }
        case KEST_L_U16: {
            uint16_t v;
            memcpy(&v, at, 2);
            out[i].integer = v;
            break;
        }
        case KEST_L_U32: {
            uint32_t v;
            memcpy(&v, at, 4);
            out[i].integer = v;
            break;
        }
        case KEST_L_F32: {
            float v;
            memcpy(&v, at, 4);
            out[i].real = v;
            break;
        }
        case KEST_L_F64: {
            double v;
            memcpy(&v, at, 8);
            out[i].real = v;
            break;
        }
        default:
            memcpy(&out[i], at, 8);
            break;
        }
    }
}

static void pack(unsigned char *to, const KestLayout *layout,
                 const KestValue *from) {
    if (layout->tagged) {
        pack_typed(to, layout->type, from);
        return;
    }
    for (uint16_t i = 0; i < layout->count; i++) {
        unsigned char *at = to + layout->pieces[i].offset;
        switch (layout->pieces[i].kind) {
        case KEST_L_I8:
        case KEST_L_U8: {
            uint8_t v = (uint8_t)from[i].integer;
            memcpy(at, &v, 1);
            break;
        }
        case KEST_L_I16:
        case KEST_L_U16: {
            uint16_t v = (uint16_t)from[i].integer;
            memcpy(at, &v, 2);
            break;
        }
        case KEST_L_I32:
        case KEST_L_U32: {
            uint32_t v = (uint32_t)from[i].integer;
            memcpy(at, &v, 4);
            break;
        }
        case KEST_L_F32: {
            float v = (float)from[i].real;
            memcpy(at, &v, 4);
            break;
        }
        case KEST_L_F64: {
            double v = from[i].real;
            memcpy(at, &v, 8);
            break;
        }
        default:
            memcpy(at, &from[i], 8);
            break;
        }
    }
}

// A slot map. Removing marks the slot dead and steps its generation, so a
// reference handed out before is recognised as stale rather than followed.
// Nothing is notified and nothing is counted; see D014.
typedef struct {
    uint32_t what;
    KestValue *elements;
    uint32_t *generations;
    bool *live;
    uint32_t *free_slots;
    uint32_t free_count;
    uint32_t used;
    uint32_t count;
    uint32_t capacity;
    uint16_t stride;
} Store;

typedef struct {
    const KestChunk *chunk;
    const uint8_t *ip;
    // Where this call's slots begin. The operand stack sits above them.
    KestValue *base;
} Frame;

struct KestRuntime {
    // Everything a call needs, kept between calls, so the host can call in
    // more than once and what the program allocated is still there.
    const KestModule *module;
    KestNative *natives;
    void **contexts;
    KestDiags *diags;
    KestValue *stack;
    KestValue *limit;
    // Separate from the arena the compiler used, so what a running program
    // allocates is visibly its own.
    KestArena *heap;
    // What the host allowed the heap, kept so a reset gets the same ceiling
    // and so a refusal can say which of the two it was.
    size_t heap_bytes;
    // The frames live in the arena rather than on the host's stack, so the
    // depth limit is Kest's own number and not whatever the host allows.
    Frame *frames;
    uint32_t frame_count;
    uint32_t stack_slots;
    uint32_t call_depth;
    // How much had been said when this started, and how much of it has been
    // written out since. What failed to compile is not this machine's to
    // report and is not reported twice.
    uint32_t said_before;
    uint32_t reported;
    // Where the machine is while a host function it called is running. A host
    // may call back in from there, and what it starts has to stand above what
    // is already on the stack rather than on top of it. NULL when nothing of
    // the program's is running. See D072.
    KestValue *running_top;
    uint32_t running_frames;
};

typedef struct KestRuntime Vm;

// What the program calls a type, which is the last piece of the name it is
// registered under. A host writes `Event`, not the module it came from.
// The program's side of a disagreement about a lend, written the way a
// declaration is, so a host can put it beside its own struct and see which
// field moved. Empty for a type that has no members to write.
static void write_shape(char *out, size_t room, KestArena *arena,
                        const KestType *type) {
    out[0] = '\0';
    size_t at = 0;
    for (uint32_t i = 0; i < type->member_count && i < 4; i++) {
        const KestMember *member = &type->members[i];
        int wrote = snprintf(out + at, room - at, "%s`%s: %s` at %u",
                             at == 0 ? "" : ", ", member->name,
                             kest_type_name(arena, member->type),
                             member->byte_offset);
        if (wrote < 0 || (size_t)wrote >= room - at) {
            return;
        }
        at += (size_t)wrote;
    }
    if (type->member_count > 4) {
        snprintf(out + at, room - at, ", and %u more", type->member_count - 4);
    }
}

// Where a type was written, when it was written anywhere: a note points at the
// declaration the host has come apart from, which is the half of the
// disagreement the host cannot see.
static void note_declaration(KestRuntime *runtime, const KestLayout *layout,
                             const char *label) {
    const KestType *type = layout->type;
    if (type == NULL || type->declared_in == NULL) {
        return;
    }
    kest_diags_note(runtime->diags, type->declared_in, type->span, "%s", label);
}

KestValue kest_borrow(KestRuntime *runtime, void *data, uint32_t length,
                      const char *element, size_t size) {
    KestValue value = {0};

    // What the program lays this type out as. Only a type the program uses as
    // an element has one, which is exactly the set that can be lent, and a
    // host asking beforehand asks the same thing.
    const KestLayout *named_layouts[2] = {NULL, NULL};
    uint32_t named = kest_module_layout_of(runtime->module, element,
                                           named_layouts, 2);
    const KestLayout *layout = named_layouts[0];
    // A lend is not in a file, so nothing is pointed at.
    KestSpan nowhere = {0, 0};
    kest_diags_in(runtime->diags, NULL);
    if (named == 0) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0610", nowhere,
                       "the program has no array of `%s` to lend to", element);
        // A lend names a type, and only a declared one has a name. A run, an
        // optional or a reference is spelled out of other types and has none,
        // so what a host lends an array of is a struct around it.
        const char *nearest = kest_module_nearest(runtime->module, element);
        if (element[0] == '[' || strchr(element, '<') != NULL ||
            strchr(element, '?') != NULL) {
            kest_diags_suggest(runtime->diags,
                               "a lend names a declared type; give it one: "
                               "`struct Row { m: %s }`",
                               element);
        } else if (nearest != NULL) {
            kest_diags_suggest(runtime->diags, "the nearest one that can be "
                                               "lent to is `%s`",
                               nearest);
        } else {
            kest_diags_suggest(runtime->diags,
                               "only a type the program holds in an array can "
                               "be lent");
        }
        return value;
    }
    if (named > 1) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0610", nowhere,
                       "more than one `%s` is in this program", element);
        for (uint32_t i = 0; i < 2; i++) {
            note_declaration(runtime, named_layouts[i], "this one");
        }
        const char *askable = kest_module_askable(runtime->module, element);
        kest_diags_suggest(runtime->diags,
                           "write the module it came from: `%s`",
                           askable == NULL ? "world.Event" : askable);
        return value;
    }
    uint16_t stride = layout->size;
    if (size != stride) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0610", nowhere,
                       "the program lays `%s` out in %u bytes and this host "
                       "has %zu",
                       element, stride, size);
        const KestType *type = layout->type;
        char shape[192];
        shape[0] = '\0';
        if (type != NULL && type->member_count > 0) {
            write_shape(shape, sizeof(shape), runtime->diags->arena, type);
        }
        note_declaration(runtime, layout,
                         shape[0] == '\0' ? "this is what it lays out" : shape);
        kest_diags_suggest(runtime->diags,
                           "the two declarations have come apart");
        return value;
    }

    Array *array = kest_arena_alloc(runtime->heap, sizeof(Array), 16);
    if (array == NULL) {
        return value;
    }
    array->what = KEST_IS_ARRAY;
    array->length = length;
    array->capacity = length;
    array->borrowed = true;
    array->stride = stride;
    // The block is the host's. The header is ours, and it points at theirs.
    array->bytes = data;
    value.object = array;
    return value;
}

// The instruction being executed, so a failure is reported at the source it
// came from rather than at the byte after it.
// Writes what a program would write to build this value, in the manner of
// snprintf: it returns the length it needed whether or not it fitted, so the
// caller measures with room of nought and then writes.
static size_t format_value(char *out, size_t room, const KestType *type,
                           const KestValue *slots);

static size_t put_text(char *out, size_t room, const char *text) {
    size_t length = strlen(text);
    for (size_t i = 0; i < length && i < room; i++) {
        out[i] = text[i];
    }
    return length;
}

// A string is written as a string, quotes and escapes and all, because what
// is being written is the source and not the content. A hole holding text on
// its own is the content, which is the exception D035 names.
static size_t put_quoted(char *out, size_t room, const char *text) {
    size_t used = 0;
    if (used < room) {
        out[used] = '"';
    }
    used++;
    for (const char *c = text; *c != '\0'; c++) {
        if (*c == '"' || *c == '\\') {
            if (used < room) {
                out[used] = '\\';
            }
            used++;
        }
        if (used < room) {
            out[used] = *c;
        }
        used++;
    }
    if (used < room) {
        out[used] = '"';
    }
    return used + 1;
}

static size_t format_flags(char *out, size_t room, const KestType *set,
                           uint64_t bits) {
    const char *named = kest_type_written(set);
    size_t used = 0;
    bool any = false;
    for (uint32_t c = 0; c < set->case_count; c++) {
        if ((bits & ((uint64_t)1 << c)) == 0) {
            continue;
        }
        if (any) {
            used += put_text(out + (used < room ? used : room),
                             used < room ? room - used : 0, " | ");
        }
        any = true;
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, named);
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, ".");
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, set->cases[c].name);
    }
    if (!any) {
        used += put_text(out, room, named);
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, "()");
    }
    return used;
}

size_t kest_write_value(char *out, size_t room, const KestType *type,
                        const KestValue *slots) {
    return format_value(out, room, type, slots);
}

static size_t format_value(char *out, size_t room, const KestType *type,
                           const KestValue *slots) {
    char buffer[64];
    switch (type->tag) {
    case KEST_T_BOOL:
        return put_text(out, room, slots[0].integer ? "true" : "false");
    case KEST_T_INT:
        if (type->is_signed) {
            snprintf(buffer, sizeof(buffer), "%lld",
                     (long long)slots[0].integer);
        } else {
            snprintf(buffer, sizeof(buffer), "%llu",
                     (unsigned long long)slots[0].integer);
        }
        return put_text(out, room, buffer);
    case KEST_T_FLOAT:
        kest_write_real(buffer, sizeof(buffer), slots[0].real,
                        type->width == 32);
        return put_text(out, room, buffer);
    case KEST_T_TEXT:
        return put_quoted(out, room, slots[0].text);
    case KEST_T_FLAGS:
        return format_flags(out, room, type, (uint64_t)slots[0].integer);
    case KEST_T_ENUM: {
        const char *named = kest_type_written(type);
        uint32_t which = (uint32_t)slots[0].integer;
        if (which >= type->case_count) {
            return put_text(out, room, named);
        }
        const KestVariantType *variant = &type->cases[which];
        size_t used = put_text(out, room, named);
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, ".");
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, variant->name);
        if (variant->payload_count == 0) {
            return used;
        }
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, "(");
        for (uint32_t p = 0; p < variant->payload_count; p++) {
            if (p > 0) {
                used += put_text(out + (used < room ? used : room),
                                 used < room ? room - used : 0, ", ");
            }
            used += format_value(out + (used < room ? used : room),
                                 used < room ? room - used : 0,
                                 variant->payload[p],
                                 slots + variant->offsets[p]);
        }
        used += put_text(out + (used < room ? used : room),
                         used < room ? room - used : 0, ")");
        return used;
    }
    // The tag is the last slot, which is where the value stops.
    case KEST_T_OPTIONAL:
        if (slots[type->element->slots].integer == 0) {
            return put_text(out, room, "none");
        }
        return format_value(out, room, type->element, slots);
    // Every other tag is written out rather than left to a `default`, so that
    // a tag added to the language cannot land here by not being mentioned:
    // `kest_type_has_text` decides what reaches this and lists the same tags,
    // and the compiler holds both lists to being every tag there is.
    case KEST_T_ERROR:
    case KEST_T_VOID:
    case KEST_T_STRUCT:
    case KEST_T_ARRAY:
    case KEST_T_FIXED:
    case KEST_T_REF:
    case KEST_T_STORE:
    case KEST_T_FN:
    case KEST_T_MODULE:
    case KEST_T_PARAM:
        break;
    }
    // Nothing reaches this: a hole and the command line both ask
    // `kest_type_has_text` first, and it says no to every tag above. It is
    // here because C wants a value and because a fault should read like one.
    return put_text(out, room, "<no text>");
}

// Two values of one type are equal when everything that makes them up is.
// This is only reached for an enum: what a case carries is compared the way
// the same types are compared on their own.
static bool values_equal(const KestType *type, const KestValue *a,
                         const KestValue *b) {
    switch (type->tag) {
    case KEST_T_FLOAT:
        return a[0].real == b[0].real;
    case KEST_T_TEXT:
        return strcmp(a[0].text, b[0].text) == 0;
    case KEST_T_ENUM: {
        if (a[0].integer != b[0].integer) {
            return false;
        }
        uint32_t which = (uint32_t)a[0].integer;
        if (which >= type->case_count) {
            return true;
        }
        const KestVariantType *variant = &type->cases[which];
        for (uint32_t p = 0; p < variant->payload_count; p++) {
            if (!values_equal(variant->payload[p], a + variant->offsets[p],
                              b + variant->offsets[p])) {
                return false;
            }
        }
        return true;
    }
    default:
        return a[0].integer == b[0].integer;
    }
}

static uint64_t mix(uint64_t bits) {
    bits ^= bits >> 33;
    bits *= 0xff51afd7ed558ccdULL;
    bits ^= bits >> 33;
    bits *= 0xc4ceb9fe1a85ec53ULL;
    bits ^= bits >> 33;
    return bits;
}

// The number standing for a value, over the same parts that decide whether
// two of them are equal. Anything else would let two equal values differ.
static uint64_t hash_value(const KestType *type, const KestValue *slots) {
    switch (type->tag) {
    case KEST_T_FLOAT:
        return mix(slots[0].real == 0.0 ? 0 : (uint64_t)slots[0].integer);
    case KEST_T_TEXT: {
        uint64_t bits = 0xcbf29ce484222325ULL;
        for (const unsigned char *c = (const unsigned char *)slots[0].text;
             *c != '\0'; c++) {
            bits ^= *c;
            bits *= 0x100000001b3ULL;
        }
        return bits;
    }
    case KEST_T_ENUM: {
        uint64_t bits = mix((uint64_t)slots[0].integer);
        uint32_t which = (uint32_t)slots[0].integer;
        if (which >= type->case_count) {
            return bits;
        }
        const KestVariantType *variant = &type->cases[which];
        for (uint32_t p = 0; p < variant->payload_count; p++) {
            bits = bits * 31 ^
                   hash_value(variant->payload[p], slots + variant->offsets[p]);
        }
        return bits;
    }
    default:
        return mix((uint64_t)slots[0].integer);
    }
}

// A handle that is not what was wanted is a host mistake rather than a
// program one: the machine carries no types, so nothing at the boundary could
// have caught it. It is caught here instead.
#define HOLD(handle, tag, what)                                              \
    do {                                                                     \
        if (!KEST_HANDLE_IS(handle, tag)) {                                  \
            fail(vmp, frame, instruction, "K0612", "this is not %s", what);  \
            return false;                                                    \
        }                                                                    \
    } while (0)

// The first live slot at or after `from`, or -1. A store hands out slots that
// go dead, so a walk of one looks rather than counts.
static int64_t live_from(const Store *store, int64_t from) {
    for (uint32_t i = from < 0 ? 0 : (uint32_t)from; i < store->used; i++) {
        if (store->live[i]) {
            return (int64_t)i;
        }
    }
    return -1;
}

static void no_room(Vm *vm, const Frame *frame, const uint8_t *instruction,
                    const KestRuntime *rt);

static void fail(Vm *vm, const Frame *frame, const uint8_t *instruction,
                 const char *code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    char message[512];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    uint32_t offset = (uint32_t)(instruction - frame->chunk->code);
    KestSpan span = {frame->chunk->origins[offset], 1};
    kest_diags_in(vm->diags, frame->chunk->source);
    // The file the instruction came from was set when it was compiled, and
    // the machine does not change it.
    kest_diags_add(vm->diags, KEST_SEVERITY_ERROR, code, span, "%s", message);
}

// An allocation that did not happen. Which of the two it was is the difference
// between a machine that has run out and a host that said this much and no
// more, and only one of those is anybody's mistake.
static void no_room(Vm *vm, const Frame *frame, const uint8_t *instruction,
                    const KestRuntime *rt) {
    if (rt->heap_bytes != 0) {
        fail(vm, frame, instruction, "K0617",
             "the program has used the %zu bytes it was given", rt->heap_bytes);
        return;
    }
    fail(vm, frame, instruction, "K0605", "out of memory");
}

static int64_t pack_ref(uint32_t generation, uint32_t index) {
    return (int64_t)(((uint64_t)generation << 32) | index);
}

// The slot a reference names, or NULL when what it named is gone.
static KestValue *resolve_ref(Store *store, int64_t handle) {
    uint32_t index = (uint32_t)((uint64_t)handle & 0xffffffffu);
    uint32_t generation = (uint32_t)((uint64_t)handle >> 32);
    if (index >= store->used || !store->live[index] ||
        store->generations[index] != generation) {
        return NULL;
    }
    return store->elements + (size_t)index * store->stride;
}

static bool grow_store(KestArena *heap, Store *store) {
    uint32_t capacity = store->capacity == 0 ? 8 : store->capacity * 2;
    KestValue *elements =
        KEST_ARENA_ARRAY(heap, KestValue, (size_t)capacity * store->stride);
    uint32_t *generations = KEST_ARENA_ARRAY(heap, uint32_t, capacity);
    bool *live = KEST_ARENA_ARRAY(heap, bool, capacity);
    uint32_t *free_slots = KEST_ARENA_ARRAY(heap, uint32_t, capacity);
    if (elements == NULL || generations == NULL || live == NULL ||
        free_slots == NULL) {
        return false;
    }
    if (store->used > 0) {
        memcpy(elements, store->elements,
               sizeof(KestValue) * store->used * store->stride);
        memcpy(generations, store->generations,
               sizeof(uint32_t) * store->used);
        memcpy(live, store->live, sizeof(bool) * store->used);
    }
    if (store->free_count > 0) {
        memcpy(free_slots, store->free_slots,
               sizeof(uint32_t) * store->free_count);
    }
    store->elements = elements;
    store->generations = generations;
    store->live = live;
    store->free_slots = free_slots;
    store->capacity = capacity;
    return true;
}

static bool execute(KestRuntime *rt, int32_t entry, uint16_t arg_slots,
                    uint16_t *returned) {
    const KestModule *module = rt->module;
    KestNative *natives = rt->natives;
    Vm *vmp = rt;

    const KestChunk *chunk = module->functions[entry];
    // Where this run of the machine starts. Nothing is running unless a host
    // function called back in, and then it starts above what that one left.
    KestValue *floor = rt->running_top != NULL ? rt->running_top : rt->stack;
    uint32_t under = rt->running_frames;
    if (under >= rt->call_depth ||
        floor + chunk->slot_count + chunk->stack_needed > rt->limit) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(rt->diags, NULL);
        kest_diags_add(rt->diags, KEST_SEVERITY_ERROR, "K0602", nowhere,
                       "there is no room to call in from here");
        return false;
    }

    rt->frame_count = under;
    Frame *frame = &rt->frames[rt->frame_count++];
    frame->chunk = chunk;
    frame->ip = chunk->code;
    frame->base = floor;

    KestValue *top = floor + (chunk->slot_count > arg_slots ? chunk->slot_count
                                                            : arg_slots);
#define READ_BYTE() (*frame->ip++)
#define READ_U16()                                                             \
    (frame->ip += 2,                                                           \
     (uint16_t)(frame->ip[-2] | ((uint16_t)frame->ip[-1] << 8)))

    // One macro per storage class rather than thirty near-identical cases.
    // The operands are already the right kind: the compiler chose which
    // instruction this is by reading the type the checker resolved.
#define BINARY_I(field, expression)                                            \
    do {                                                                       \
        KestValue right = *--top;                                              \
        KestValue left = *--top;                                               \
        (top++)->field = (expression);                                         \
    } while (0)

    while (true) {
        const uint8_t *instruction = frame->ip;
        switch (READ_BYTE()) {
        case KEST_OP_CONST:
            *top++ = frame->chunk->constants[READ_U16()];
            break;
        case KEST_OP_CONST_RUN: {
            const KestValue *from = &frame->chunk->constants[READ_U16()];
            uint16_t count = READ_U16();
            memcpy(top, from, sizeof(KestValue) * count);
            top += count;
            break;
        }
        case KEST_OP_CONST_AT: {
            uint16_t first = READ_U16();
            uint16_t stride = READ_U16();
            uint16_t count = READ_U16();
            int64_t index = (--top)->integer;
            if (index < 0 || (uint64_t)index >= count) {
                fail(vmp, frame, instruction, "K0604",
                     "index %lld is outside %u of them", (long long)index,
                     count);
                return false;
            }
            memcpy(top,
                   &frame->chunk->constants[first + (size_t)index * stride],
                   sizeof(KestValue) * stride);
            top += stride;
            break;
        }
        case KEST_OP_LOAD:
            *top++ = frame->base[READ_U16()];
            break;
        case KEST_OP_STORE:
            frame->base[READ_U16()] = *--top;
            break;
        case KEST_OP_LOADN: {
            uint16_t slot = READ_U16();
            uint16_t count = READ_U16();
            memcpy(top, frame->base + slot, sizeof(KestValue) * count);
            top += count;
            break;
        }
        case KEST_OP_STOREN: {
            uint16_t slot = READ_U16();
            uint16_t count = READ_U16();
            top -= count;
            memcpy(frame->base + slot, top, sizeof(KestValue) * count);
            break;
        }
        case KEST_OP_FIELD: {
            uint16_t offset = READ_U16();
            uint16_t size = READ_U16();
            uint16_t total = READ_U16();
            KestValue *value = top - total;
            memmove(value, value + offset, sizeof(KestValue) * size);
            top = value + size;
            break;
        }
        case KEST_OP_ARRAY: {
            uint16_t count = READ_U16();
            const KestLayout *layout = &module->layouts[READ_U16()];
            Array *array = kest_arena_alloc(rt->heap, sizeof(Array), 16);
            unsigned char *bytes = kest_arena_alloc(
                rt->heap, (size_t)count * layout->size + 1, 16);
            if (array == NULL || bytes == NULL) {
                no_room(vmp, frame, instruction, rt);
                return false;
            }
            array->what = KEST_IS_ARRAY;
            array->length = count;
            array->capacity = count;
            array->stride = layout->size;
            array->bytes = bytes;

            top -= (size_t)count * layout->count;
            for (uint16_t i = 0; i < count; i++) {
                pack(bytes + (size_t)i * layout->size, layout,
                     top + (size_t)i * layout->count);
            }
            (top++)->object = array;
            break;
        }
        case KEST_OP_MAKE_ARRAY: {
            const KestLayout *layout = &module->layouts[READ_U16()];
            top -= layout->count;
            KestValue *fill = top;
            int64_t count = (--top)->integer;
            if (count < 0) {
                fail(vmp, frame, instruction, "K0604",
                     "an array cannot have %lld elements", (long long)count);
                return false;
            }

            Array *array = kest_arena_alloc(rt->heap, sizeof(Array), 16);
            unsigned char *bytes = kest_arena_alloc(
                rt->heap, (size_t)count * layout->size + 1, 16);
            if (array == NULL || bytes == NULL) {
                no_room(vmp, frame, instruction, rt);
                return false;
            }
            array->what = KEST_IS_ARRAY;
            array->length = (uint32_t)count;
            array->capacity = (uint32_t)count;
            array->stride = layout->size;
            array->bytes = bytes;
            for (int64_t i = 0; i < count; i++) {
                pack(bytes + (size_t)i * layout->size, layout, fill);
            }
            (top++)->object = array;
            break;
        }
        case KEST_OP_PUSH: {
            const KestLayout *layout = &module->layouts[READ_U16()];
            top -= layout->count;
            KestValue *value = top;
            Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");

            if (array->borrowed) {
                fail(vmp, frame, instruction, "K0608",
                     "this array is the host's, so it cannot grow");
                return false;
            }
            if (array->length == array->capacity) {
                uint32_t capacity = array->capacity == 0 ? 8
                                                         : array->capacity * 2;
                unsigned char *bytes = kest_arena_alloc(
                    rt->heap, (size_t)capacity * layout->size + 1, 16);
                if (bytes == NULL) {
                    no_room(vmp, frame, instruction, rt);
                    return false;
                }
                if (array->length > 0) {
                    memcpy(bytes, array->bytes,
                           (size_t)array->length * layout->size);
                }
                // The handle is the header, and the header is what moved
                // nothing, so every reference to this array sees the growth.
                array->bytes = bytes;
                array->capacity = capacity;
            }
            pack(array->bytes + (size_t)array->length * layout->size, layout,
                 value);
            array->length++;
            break;
        }
        case KEST_OP_INDEX: {
            const KestLayout *layout = &module->layouts[READ_U16()];
            int64_t index = (--top)->integer;
            const Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            if (index < 0 || (uint64_t)index >= array->length) {
                fail(vmp, frame, instruction, "K0604",
                     "index %lld is outside an array of length %u",
                     (long long)index, array->length);
                return false;
            }
            unpack(top, layout, array->bytes + (size_t)index * array->stride);
            top += layout->count;
            break;
        }
        case KEST_OP_POP_LAST: {
            const KestLayout *layout = &module->layouts[READ_U16()];
            Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            if (array->borrowed) {
                fail(vmp, frame, instruction, "K0608",
                     "this array is the host's, so it cannot shrink");
                return false;
            }
            if (array->length == 0) {
                for (uint16_t i = 0; i < layout->count; i++) {
                    top[i].integer = 0;
                }
                top += layout->count;
                (top++)->integer = 0;
                break;
            }
            array->length--;
            unpack(top, layout,
                   array->bytes + (size_t)array->length * array->stride);
            top += layout->count;
            (top++)->integer = 1;
            break;
        }
        case KEST_OP_TAKE: {
            const KestLayout *layout = &module->layouts[READ_U16()];
            int64_t index = (--top)->integer;
            Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            if (array->borrowed) {
                fail(vmp, frame, instruction, "K0608",
                     "this array is the host's, so it cannot shrink");
                return false;
            }
            if (index < 0 || (uint64_t)index >= array->length) {
                fail(vmp, frame, instruction, "K0604",
                     "index %lld is outside an array of length %u",
                     (long long)index, array->length);
                return false;
            }
            unsigned char *at = array->bytes + (size_t)index * array->stride;
            unpack(top, layout, at);
            top += layout->count;
            // What is after it keeps its order, which is the whole difference
            // between this and a store: a position here means something.
            memmove(at, at + array->stride,
                    (size_t)(array->length - index - 1) * array->stride);
            array->length--;
            break;
        }
        case KEST_OP_CLEAR: {
            Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            if (array->borrowed) {
                fail(vmp, frame, instruction, "K0608",
                     "this array is the host's, so it cannot shrink");
                return false;
            }
            array->length = 0;
            break;
        }
        case KEST_OP_ELEM_ADDR: {
            READ_U16();
            int64_t index = (--top)->integer;
            Array *array = (--top)->object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            if (index < 0 || (uint64_t)index >= array->length) {
                fail(vmp, frame, instruction, "K0604",
                     "index %lld is outside an array of length %u",
                     (long long)index, array->length);
                return false;
            }
            (top++)->object = array->bytes + (size_t)index * array->stride;
            break;
        }
        case KEST_OP_LOAD_SLOTS: {
            uint16_t base = READ_U16();
            uint16_t stride = READ_U16();
            uint16_t count = READ_U16();
            int64_t index = (--top)->integer;
            if (index < 0 || (uint64_t)index >= count) {
                fail(vmp, frame, instruction, "K0604",
                     "index %lld is outside %u of them", (long long)index,
                     count);
                return false;
            }
            memcpy(top, frame->base + base + (size_t)index * stride,
                   sizeof(KestValue) * stride);
            top += stride;
            break;
        }
        case KEST_OP_STORE_SLOTS: {
            uint16_t base = READ_U16();
            uint16_t stride = READ_U16();
            uint16_t count = READ_U16();
            top -= stride;
            KestValue *value = top;
            int64_t index = (--top)->integer;
            if (index < 0 || (uint64_t)index >= count) {
                fail(vmp, frame, instruction, "K0604",
                     "index %lld is outside %u of them", (long long)index,
                     count);
                return false;
            }
            memcpy(frame->base + base + (size_t)index * stride, value,
                   sizeof(KestValue) * stride);
            break;
        }
        case KEST_OP_OFFSET_ADDR: {
            uint16_t stride = READ_U16();
            uint16_t count = READ_U16();
            int64_t index = (--top)->integer;
            unsigned char *at = (--top)->object;
            if (index < 0 || (uint64_t)index >= count) {
                fail(vmp, frame, instruction, "K0604",
                     "index %lld is outside %u of them", (long long)index,
                     count);
                return false;
            }
            (top++)->object = at + (size_t)index * stride;
            break;
        }
        case KEST_OP_LOAD_AT: {
            uint16_t offset = READ_U16();
            const KestLayout *layout = &module->layouts[READ_U16()];
            const unsigned char *at = (--top)->object;
            unpack(top, layout, at + offset);
            top += layout->count;
            break;
        }
        case KEST_OP_STORE_AT: {
            uint16_t offset = READ_U16();
            const KestLayout *layout = &module->layouts[READ_U16()];
            top -= layout->count;
            KestValue *value = top;
            unsigned char *at = (--top)->object;
            pack(at + offset, layout, value);
            break;
        }
        case KEST_OP_NEW_STORE: {
            Store *store = kest_arena_alloc(rt->heap, sizeof(Store), 16);
            if (store == NULL) {
                no_room(vmp, frame, instruction, rt);
                return false;
            }
            store->what = KEST_IS_STORE;
            store->stride = READ_U16();
            (top++)->object = store;
            break;
        }
        case KEST_OP_ADD: {
            uint16_t stride = READ_U16();
            top -= stride;
            KestValue *value = top;
            Store *store = (--top)->object;
            HOLD(store, KEST_IS_STORE, "a store");

            uint32_t index;
            if (store->free_count > 0) {
                index = store->free_slots[--store->free_count];
            } else {
                if (store->used == store->capacity &&
                    !grow_store(rt->heap, store)) {
                    no_room(vmp, frame, instruction, rt);
                    kest_arena_free(rt->heap);
                    return false;
                }
                index = store->used++;
                store->generations[index] = 1;
            }
            store->live[index] = true;
            store->count++;
            memcpy(store->elements + (size_t)index * stride, value,
                   sizeof(KestValue) * stride);
            (top++)->integer = pack_ref(store->generations[index], index);
            break;
        }
        case KEST_OP_GET: {
            uint16_t stride = READ_U16();
            int64_t handle = (--top)->integer;
            Store *store = (--top)->object;
            HOLD(store, KEST_IS_STORE, "a store");
            const KestValue *at = resolve_ref(store, handle);
            if (at == NULL) {
                for (uint16_t i = 0; i < stride; i++) {
                    (top++)->integer = 0;
                }
                (top++)->integer = 0;
            } else {
                memcpy(top, at, sizeof(KestValue) * stride);
                top += stride;
                (top++)->integer = 1;
            }
            break;
        }
        case KEST_OP_SET: {
            uint16_t stride = READ_U16();
            top -= stride;
            KestValue *value = top;
            int64_t handle = (--top)->integer;
            Store *store = (--top)->object;
            HOLD(store, KEST_IS_STORE, "a store");
            KestValue *at = resolve_ref(store, handle);
            if (at != NULL) {
                memcpy(at, value, sizeof(KestValue) * stride);
            }
            (top++)->integer = at != NULL;
            break;
        }
        case KEST_OP_REMOVE: {
            int64_t handle = (--top)->integer;
            Store *store = (--top)->object;
            HOLD(store, KEST_IS_STORE, "a store");
            if (resolve_ref(store, handle) == NULL) {
                (top++)->integer = 0;
                break;
            }
            uint32_t index = (uint32_t)((uint64_t)handle & 0xffffffffu);
            store->live[index] = false;
            store->generations[index]++;
            store->free_slots[store->free_count++] = index;
            store->count--;
            (top++)->integer = 1;
            break;
        }
        case KEST_OP_SEEK_FROM:
        case KEST_OP_SEEK_NEXT: {
            bool first = *instruction == KEST_OP_SEEK_FROM;
            uint16_t which = READ_U16();
            uint16_t at = READ_U16();
            uint16_t away = READ_U16();
            const Store *store = frame->base[which].object;
            HOLD(store, KEST_IS_STORE, "a store");
            int64_t from = frame->base[at].integer + (first ? 0 : 1);
            int64_t found = live_from(store, from);
            frame->base[at].integer = found;
            // The first one leaves when there is none and the ones after go
            // back while there is one, which is the same shape every other
            // walk has: a test above the loop and a test at the bottom.
            if (first) {
                if (found < 0) {
                    frame->ip += away;
                }
            } else if (found >= 0) {
                frame->ip -= away;
            }
            break;
        }
        case KEST_OP_STORE_REF: {
            uint32_t index = (uint32_t)(--top)->integer;
            const Store *store = (--top)->object;
            HOLD(store, KEST_IS_STORE, "a store");
            (top++)->integer = pack_ref(store->generations[index], index);
            break;
        }
        case KEST_OP_COUNT: {
            const Store *store = top[-1].object;
            HOLD(store, KEST_IS_STORE, "a store");
            top[-1].integer = store->count;
            break;
        }
        case KEST_OP_TEXT_FLAGS:
        case KEST_OP_TEXT_ENUM: {
            // Written the way it is built. Every other value's text is the
            // source that makes it and these are no different; D035 says so
            // for a set of bits and D036 for the cases of an enum.
            const KestType *type = module->layout_types[READ_U16()];
            uint16_t held = type->slots;
            top -= held;
            size_t length = format_value(NULL, 0, type, top);
            char *text = kest_arena_alloc(rt->heap, length + 1, 1);
            if (text == NULL) {
                no_room(vmp, frame, instruction, rt);
                return false;
            }
            format_value(text, length, type, top);
            text[length] = '\0';
            (top++)->text = text;
            break;
        }
        case KEST_OP_TEXT_I:
        case KEST_OP_TEXT_U:
        case KEST_OP_TEXT_F:
        case KEST_OP_TEXT_F32:
        case KEST_OP_TEXT_B: {
            char buffer[64];
            int written;
            if (instruction[0] == KEST_OP_TEXT_I) {
                written = snprintf(buffer, sizeof(buffer), "%lld",
                                   (long long)top[-1].integer);
            } else if (instruction[0] == KEST_OP_TEXT_U) {
                written = snprintf(buffer, sizeof(buffer), "%llu",
                                   (unsigned long long)top[-1].integer);
            } else if (instruction[0] == KEST_OP_TEXT_F ||
                       instruction[0] == KEST_OP_TEXT_F32) {
                written = kest_write_real(buffer, sizeof(buffer), top[-1].real,
                                          instruction[0] == KEST_OP_TEXT_F32);
            } else {
                written = snprintf(buffer, sizeof(buffer), "%s",
                                   top[-1].integer ? "true" : "false");
            }
            char *text = kest_arena_alloc(rt->heap, (size_t)written + 1, 1);
            if (text == NULL) {
                no_room(vmp, frame, instruction, rt);
                return false;
            }
            memcpy(text, buffer, (size_t)written + 1);
            top[-1].text = text;
            break;
        }
        case KEST_OP_CONCAT: {
            uint16_t count = READ_U16();
            top -= count;
            size_t length = 0;
            for (uint16_t i = 0; i < count; i++) {
                length += strlen(top[i].text);
            }
            char *text = kest_arena_alloc(rt->heap, length + 1, 1);
            if (text == NULL) {
                no_room(vmp, frame, instruction, rt);
                return false;
            }
            size_t used = 0;
            for (uint16_t i = 0; i < count; i++) {
                size_t piece = strlen(top[i].text);
                memcpy(text + used, top[i].text, piece);
                used += piece;
            }
            text[used] = '\0';
            (top++)->text = text;
            break;
        }
        case KEST_OP_TEXT_FROM: {
            const Array *bytes = (--top)->object;
            HOLD(bytes, KEST_IS_ARRAY, "an array");
            char *text = kest_arena_alloc(rt->heap, bytes->length + 1, 1);
            if (text == NULL) {
                no_room(vmp, frame, instruction, rt);
                return false;
            }
            // Text ends at its first zero byte, so one in the middle would
            // quietly cut the rest off. Saying so beats losing it.
            for (uint32_t i = 0; i < bytes->length; i++) {
                if (bytes->bytes[i] == 0) {
                    fail(vmp, frame, instruction, "K0604",
                         "byte %u is zero, and text ends at a zero byte", i);
                    return false;
                }
            }
            memcpy(text, bytes->bytes, bytes->length);
            text[bytes->length] = '\0';
            (top++)->text = text;
            break;
        }
        // One round of a mixer over the bits, which is what a table wants of
        // a number that is often small and often consecutive.
        case KEST_OP_HASH_I:
        case KEST_OP_HASH_F: {
            uint64_t bits = (uint64_t)top[-1].integer;
            if (instruction[0] == KEST_OP_HASH_F && top[-1].real == 0.0) {
                // Nought and minus nought are one value to `==`, so they are
                // one value here.
                bits = 0;
            }
            top[-1].integer = (int64_t)mix(bits);
            break;
        }
        case KEST_OP_HASH_T: {
            // FNV-1a over the bytes, because text is its bytes (D021) and two
            // pieces that compare equal are the same bytes.
            const char *text = top[-1].text;
            uint64_t bits = 0xcbf29ce484222325ULL;
            for (const unsigned char *c = (const unsigned char *)text;
                 *c != '\0'; c++) {
                bits ^= *c;
                bits *= 0x100000001b3ULL;
            }
            top[-1].integer = (int64_t)bits;
            break;
        }
        case KEST_OP_HASH_ENUM: {
            const KestType *type = module->layout_types[READ_U16()];
            top -= type->slots;
            uint64_t bits = hash_value(type, top);
            (top++)->integer = (int64_t)bits;
            break;
        }
        case KEST_OP_EQ_ENUM:
        case KEST_OP_NE_ENUM: {
            const KestType *type = module->layout_types[READ_U16()];
            top -= type->slots;
            const KestValue *right = top;
            top -= type->slots;
            bool same = values_equal(type, top, right);
            (top++)->integer = instruction[0] == KEST_OP_EQ_ENUM ? same : !same;
            break;
        }
        case KEST_OP_TEXT_LEN:
            top[-1].integer = (int64_t)strlen(top[-1].text);
            break;
        case KEST_OP_TEXT_AT: {
            int64_t index = (--top)->integer;
            const char *text = (--top)->text;
            size_t length = strlen(text);
            if (index < 0 || (uint64_t)index >= length) {
                fail(vmp, frame, instruction, "K0604",
                     "index %lld is outside text of %zu bytes",
                     (long long)index, length);
                return false;
            }
            (top++)->integer = (unsigned char)text[index];
            break;
        }
        case KEST_OP_TEXT_IN: {
            const char *text = frame->base[READ_U16()].text;
            int64_t index = frame->base[READ_U16()].integer;
            (top++)->integer = (unsigned char)text[index];
            break;
        }
        case KEST_OP_TEXT_SLICE: {
            int64_t count = (--top)->integer;
            int64_t from = (--top)->integer;
            const char *text = (--top)->text;
            size_t length = strlen(text);

            if (from < 0 || count < 0 || (uint64_t)from > length ||
                (uint64_t)(from + count) > length) {
                fail(vmp, frame, instruction, "K0604",
                     "%lld bytes from %lld is outside text of %zu bytes",
                     (long long)count, (long long)from, length);
                return false;
            }
            char *piece = kest_arena_alloc(rt->heap, (size_t)count + 1, 1);
            if (piece == NULL) {
                no_room(vmp, frame, instruction, rt);
                return false;
            }
            memcpy(piece, text + from, (size_t)count);
            piece[count] = '\0';
            (top++)->text = piece;
            break;
        }
        case KEST_OP_TEXT_REST: {
            int64_t at = (--top)->integer;
            const char *text = (--top)->text;
            // Walked to rather than measured, because what this costs is the
            // part being stepped over and not the part being kept. A loop that
            // takes the rest of the rest reads each byte once between them.
            int64_t seen = 0;
            while (seen < at && text[seen] != '\0') {
                seen++;
            }
            if (at < 0 || seen < at) {
                // Measured only to say so. Walking to a place that is not
                // there costs what is there; saying how much that was costs
                // nothing that matters, because the program is stopping.
                fail(vmp, frame, instruction, "K0604",
                     "the rest from %lld is outside text of %zu bytes",
                     (long long)at, strlen(text));
                return false;
            }
            (top++)->text = text + at;
            break;
        }
        case KEST_OP_TEXT_MATCHES: {
            const char *needle = (--top)->text;
            int64_t at = (--top)->integer;
            const char *text = (--top)->text;
            int64_t seen = 0;
            while (seen < at && text[seen] != '\0') {
                seen++;
            }
            if (at < 0 || seen < at) {
                fail(vmp, frame, instruction, "K0604",
                     "looking at %lld, which is outside text of %zu bytes",
                     (long long)at, strlen(text));
                return false;
            }
            const char *from = text + at;
            size_t i = 0;
            while (needle[i] != '\0' && from[i] == needle[i]) {
                i++;
            }
            (top++)->integer = needle[i] == '\0';
            break;
        }
        case KEST_OP_TEXT_FIND: {
            int64_t from = (--top)->integer;
            const char *needle = (--top)->text;
            const char *haystack = (--top)->text;
            size_t length = strlen(haystack);
            if (from < 0 || (uint64_t)from > length) {
                fail(vmp, frame, instruction, "K0604",
                     "looking from %lld, which is outside text of %zu bytes",
                     (long long)from, length);
                return false;
            }
            const char *at = strstr(haystack + from, needle);
            (top++)->integer = at == NULL ? 0 : (int64_t)(at - haystack);
            (top++)->integer = at != NULL;
            break;
        }
        case KEST_OP_LEN: {
            const Array *array = top[-1].object;
            HOLD(array, KEST_IS_ARRAY, "an array");
            top[-1].integer = array->length;
            break;
        }
        case KEST_OP_TRUE:
            (top++)->integer = 1;
            break;
        case KEST_OP_FALSE:
            (top++)->integer = 0;
            break;
        case KEST_OP_POP:
            top--;
            break;
        case KEST_OP_POPN:
            top -= READ_U16();
            break;
        case KEST_OP_ROTATE: {
            // The last slot is the tag and belongs first, so the run is
            // rolled by one rather than reversed.
            uint16_t count = READ_U16();
            KestValue tag = top[-1];
            memmove(top - count + 1, top - count,
                    sizeof(KestValue) * (size_t)(count - 1));
            top[-count] = tag;
            break;
        }
        case KEST_OP_DUP:
            *top = top[-1];
            top++;
            break;

        case KEST_OP_ADD_I:
            BINARY_I(integer, left.integer + right.integer);
            break;
        case KEST_OP_SUB_I:
            BINARY_I(integer, left.integer - right.integer);
            break;
        case KEST_OP_MUL_I:
            BINARY_I(integer, left.integer * right.integer);
            break;
        case KEST_OP_DIV_I:
        case KEST_OP_MOD_I: {
            KestValue right = *--top;
            KestValue left = *--top;
            if (right.integer == 0) {
                fail(vmp, frame, instruction, "K0601", "division by zero");
                return false;
            }
            // The one pair of operands whose quotient does not fit, which on
            // most machines traps rather than wrapping.
            if (left.integer == INT64_MIN && right.integer == -1) {
                (top++)->integer =
                    instruction[0] == KEST_OP_DIV_I ? INT64_MIN : 0;
                break;
            }
            (top++)->integer = instruction[0] == KEST_OP_DIV_I
                                   ? left.integer / right.integer
                                   : left.integer % right.integer;
            break;
        }
        case KEST_OP_DIV_U:
        case KEST_OP_MOD_U: {
            KestValue right = *--top;
            KestValue left = *--top;
            if (right.integer == 0) {
                fail(vmp, frame, instruction, "K0601", "division by zero");
                return false;
            }
            uint64_t a = (uint64_t)left.integer;
            uint64_t b = (uint64_t)right.integer;
            (top++)->integer =
                (int64_t)(instruction[0] == KEST_OP_DIV_U ? a / b : a % b);
            break;
        }
        case KEST_OP_AND_I:
            top--;
            top[-1].integer &= top[0].integer;
            break;
        case KEST_OP_OR_I:
            top--;
            top[-1].integer |= top[0].integer;
            break;
        case KEST_OP_XOR_I:
            top--;
            top[-1].integer ^= top[0].integer;
            break;
        case KEST_OP_NOT_I:
            top[-1].integer = ~top[-1].integer;
            break;
        // A shift is done in a slot and narrowed after, the same way every
        // other arithmetic is (D018). A count past the width of the slot has
        // no meaning in C, so it is answered here rather than left to the
        // machine: everything shifts out.
        case KEST_OP_SHL: {
            int64_t by = (--top)->integer;
            if (by < 0) {
                fail(vmp, frame, instruction, "K0604",
                     "a shift of %lld is not a count", (long long)by);
                return false;
            }
            top[-1].integer =
                by >= 64 ? 0 : (int64_t)((uint64_t)top[-1].integer << by);
            break;
        }
        case KEST_OP_SHR_I: {
            int64_t by = (--top)->integer;
            if (by < 0) {
                fail(vmp, frame, instruction, "K0604",
                     "a shift of %lld is not a count", (long long)by);
                return false;
            }
            // Signed, so the sign is what shifts in and a negative number
            // stays negative however far it goes.
            int64_t value = top[-1].integer;
            top[-1].integer = by >= 64 ? (value < 0 ? -1 : 0) : value >> by;
            break;
        }
        case KEST_OP_SHR_U: {
            int64_t by = (--top)->integer;
            if (by < 0) {
                fail(vmp, frame, instruction, "K0604",
                     "a shift of %lld is not a count", (long long)by);
                return false;
            }
            uint64_t value = (uint64_t)top[-1].integer;
            top[-1].integer = by >= 64 ? 0 : (int64_t)(value >> by);
            break;
        }
        case KEST_OP_NEG_I:
            top[-1].integer = -top[-1].integer;
            break;
        case KEST_OP_I2F:
            top[-1].real = (double)top[-1].integer;
            break;
        case KEST_OP_U2F:
            top[-1].real = (double)(uint64_t)top[-1].integer;
            break;
        case KEST_OP_TO_F32:
            top[-1].real = (double)(float)top[-1].real;
            break;
        case KEST_OP_F2I: {
            // C leaves a value outside the range undefined. This does not: it
            // stops at the end, which is the answer every reader expects and
            // the only one that is the same on every machine.
            double value = top[-1].real;
            uint16_t kind = READ_U16();
            double low;
            double high;
            switch (kind) {
            case KEST_L_I8:
                low = -128.0;
                high = 127.0;
                break;
            case KEST_L_I16:
                low = -32768.0;
                high = 32767.0;
                break;
            case KEST_L_I32:
                low = -2147483648.0;
                high = 2147483647.0;
                break;
            case KEST_L_U8:
                low = 0.0;
                high = 255.0;
                break;
            case KEST_L_U16:
                low = 0.0;
                high = 65535.0;
                break;
            case KEST_L_U32:
                low = 0.0;
                high = 4294967295.0;
                break;
            case KEST_L_U64:
                low = 0.0;
                high = 18446744073709551615.0;
                break;
            default:
                low = -9223372036854775808.0;
                high = 9223372036854775807.0;
                break;
            }
            if (value != value) {
                top[-1].integer = 0;
            } else if (value <= low) {
                top[-1].integer = kind == KEST_L_I64
                                      ? INT64_MIN
                                      : (int64_t)low;
            } else if (value >= high) {
                top[-1].integer = kind == KEST_L_U64  ? (int64_t)UINT64_MAX
                                  : kind == KEST_L_I64 ? INT64_MAX
                                                       : (int64_t)high;
            } else {
                top[-1].integer = (int64_t)value;
            }
            break;
        }
        case KEST_OP_NARROW: {
            // Every integer in a slot is kept at its declared width, sign
            // extended or zero extended, so a comparison and a division do not
            // each need to know how wide it is.
            int64_t value = top[-1].integer;
            switch (READ_U16()) {
            case KEST_L_I8:
                top[-1].integer = (int8_t)value;
                break;
            case KEST_L_I16:
                top[-1].integer = (int16_t)value;
                break;
            case KEST_L_I32:
                top[-1].integer = (int32_t)value;
                break;
            case KEST_L_U8:
                top[-1].integer = (uint8_t)value;
                break;
            case KEST_L_U16:
                top[-1].integer = (uint16_t)value;
                break;
            case KEST_L_U32:
                top[-1].integer = (uint32_t)value;
                break;
            default:
                break;
            }
            break;
        }

        case KEST_OP_ADD_F:
            BINARY_I(real, left.real + right.real);
            break;
        case KEST_OP_SUB_F:
            BINARY_I(real, left.real - right.real);
            break;
        case KEST_OP_MUL_F:
            BINARY_I(real, left.real * right.real);
            break;
        case KEST_OP_DIV_F:
            BINARY_I(real, left.real / right.real);
            break;
        case KEST_OP_NEG_F:
            top[-1].real = -top[-1].real;
            break;

        case KEST_OP_ADD_F32:
            BINARY_I(real, (double)((float)left.real + (float)right.real));
            break;
        case KEST_OP_SUB_F32:
            BINARY_I(real, (double)((float)left.real - (float)right.real));
            break;
        case KEST_OP_MUL_F32:
            BINARY_I(real, (double)((float)left.real * (float)right.real));
            break;
        case KEST_OP_DIV_F32:
            BINARY_I(real, (double)((float)left.real / (float)right.real));
            break;
        case KEST_OP_NEG_F32:
            top[-1].real = (double)(-(float)top[-1].real);
            break;

        case KEST_OP_LT_I:
            BINARY_I(integer, left.integer < right.integer);
            break;
        case KEST_OP_LE_I:
            BINARY_I(integer, left.integer <= right.integer);
            break;
        case KEST_OP_GT_I:
            BINARY_I(integer, left.integer > right.integer);
            break;
        case KEST_OP_GE_I:
            BINARY_I(integer, left.integer >= right.integer);
            break;
        case KEST_OP_LT_U:
            BINARY_I(integer, (uint64_t)left.integer < (uint64_t)right.integer);
            break;
        case KEST_OP_LE_U:
            BINARY_I(integer,
                     (uint64_t)left.integer <= (uint64_t)right.integer);
            break;
        case KEST_OP_GT_U:
            BINARY_I(integer, (uint64_t)left.integer > (uint64_t)right.integer);
            break;
        case KEST_OP_GE_U:
            BINARY_I(integer,
                     (uint64_t)left.integer >= (uint64_t)right.integer);
            break;
        case KEST_OP_LT_F:
            BINARY_I(integer, left.real < right.real);
            break;
        case KEST_OP_LE_F:
            BINARY_I(integer, left.real <= right.real);
            break;
        case KEST_OP_GT_F:
            BINARY_I(integer, left.real > right.real);
            break;
        case KEST_OP_GE_F:
            BINARY_I(integer, left.real >= right.real);
            break;

        case KEST_OP_EQ_I:
            BINARY_I(integer, left.integer == right.integer);
            break;
        case KEST_OP_NE_I:
            BINARY_I(integer, left.integer != right.integer);
            break;
        case KEST_OP_EQ_F:
            BINARY_I(integer, left.real == right.real);
            break;
        case KEST_OP_NE_F:
            BINARY_I(integer, left.real != right.real);
            break;
        case KEST_OP_EQ_T:
            BINARY_I(integer, strcmp(left.text, right.text) == 0);
            break;
        case KEST_OP_NE_T:
            BINARY_I(integer, strcmp(left.text, right.text) != 0);
            break;

        case KEST_OP_LT_T:
            BINARY_I(integer, strcmp(left.text, right.text) < 0);
            break;
        case KEST_OP_LE_T:
            BINARY_I(integer, strcmp(left.text, right.text) <= 0);
            break;
        case KEST_OP_GT_T:
            BINARY_I(integer, strcmp(left.text, right.text) > 0);
            break;
        case KEST_OP_GE_T:
            BINARY_I(integer, strcmp(left.text, right.text) >= 0);
            break;

        case KEST_OP_NOT:
            top[-1].integer = !top[-1].integer;
            break;

        case KEST_OP_JUMP: {
            // Read the distance before moving, because the read moves too.
            uint16_t distance = READ_U16();
            frame->ip += distance;
            break;
        }
        case KEST_OP_JUMP_FALSE: {
            uint16_t distance = READ_U16();
            if ((--top)->integer == 0) {
                frame->ip += distance;
            }
            break;
        }
        case KEST_OP_LOOP: {
            uint16_t distance = READ_U16();
            frame->ip -= distance;
            break;
        }

        case KEST_OP_NEXT_LESS_I: {
            uint16_t slot = READ_U16();
            uint16_t limit = READ_U16();
            uint16_t distance = READ_U16();
            if (++frame->base[slot].integer < frame->base[limit].integer) {
                frame->ip -= distance;
            }
            break;
        }

        case KEST_OP_NEXT_LESS_U: {
            uint16_t slot = READ_U16();
            uint16_t limit = READ_U16();
            uint16_t distance = READ_U16();
            uint64_t next = (uint64_t)++frame->base[slot].integer;
            if (next < (uint64_t)frame->base[limit].integer) {
                frame->ip -= distance;
            }
            break;
        }

        case KEST_OP_CALL: {
            uint16_t index = READ_U16();
            uint16_t argument_slots = READ_U16();
            const KestChunk *callee = module->functions[index];

            if (rt->frame_count == rt->call_depth) {
                fail(vmp, frame, instruction, "K0602",
                     "calls nest more than %u deep", rt->call_depth);
                return false;
            }
            KestValue *base = top - argument_slots;
            if (base + callee->slot_count + callee->stack_needed > rt->limit) {
                fail(vmp, frame, instruction, "K0602", "out of stack");
                return false;
            }

            frame = &rt->frames[rt->frame_count++];
            frame->chunk = callee;
            frame->ip = callee->code;
            frame->base = base;
            top = base + callee->slot_count;
            break;
        }

        case KEST_OP_CALL_VALUE: {
            uint16_t argument_slots = READ_U16();
            int64_t which = (--top)->integer;
            if (which < 0 || (uint64_t)which >= module->count) {
                fail(vmp, frame, instruction, "K0609",
                     "this is not a function");
                return false;
            }
            const KestChunk *callee = module->functions[which];

            if (rt->frame_count == rt->call_depth) {
                fail(vmp, frame, instruction, "K0602",
                     "calls nest more than %u deep", rt->call_depth);
                return false;
            }
            KestValue *base = top - argument_slots;
            if (base + callee->slot_count + callee->stack_needed > rt->limit) {
                fail(vmp, frame, instruction, "K0602", "out of stack");
                return false;
            }

            frame = &rt->frames[rt->frame_count++];
            frame->chunk = callee;
            frame->ip = callee->code;
            frame->base = base;
            top = base + callee->slot_count;
            break;
        }

        case KEST_OP_CALL_HOST: {
            uint16_t index = READ_U16();
            uint16_t argument_slots = READ_U16();
            uint16_t result_slots = READ_U16();
            KestValue *base = top - argument_slots;
            // The same convention a Kest call uses: the arguments are where
            // the result goes. Where the machine is, is written down first,
            // because the host may call back in from inside this.
            KestValue *was_top = rt->running_top;
            uint32_t was_frames = rt->running_frames;
            rt->running_top = top;
            rt->running_frames = rt->frame_count;
            natives[index](base, rt, rt->contexts[index]);
            // A call back in unwound to exactly where it started, so there
            // is nothing to put back but where the machine was.
            rt->running_top = was_top;
            rt->running_frames = was_frames;
            top = base + result_slots;
            break;
        }

        case KEST_OP_RETURN: {
            uint16_t count = READ_U16();
            // The result lands where the arguments were, which is where the
            // caller left room for it.
            KestValue *base = frame->base;
            memmove(base, top - count, sizeof(KestValue) * count);

            rt->frame_count--;
            if (rt->frame_count == under) {
                *returned = count;
                return true;
            }
            frame = &rt->frames[rt->frame_count - 1];
            top = base + count;
            break;
        }
        }
    }

#undef READ_BYTE
#undef READ_U16
#undef BINARY_I
}

KestRuntime *kest_runtime_new(KestArena *arena, const KestModule *module,
                              const KestHost *host, KestDiags *diags,
                              const KestLimits *limits) {
    KestRuntime *rt = KEST_ARENA_NEW(arena, KestRuntime);
    if (rt == NULL) {
        return NULL;
    }
    rt->module = module;
    rt->diags = diags;
    rt->said_before = diags->count;
    rt->reported = diags->count;
    rt->stack_slots = limits == NULL || limits->stack_slots == 0
                          ? STACK_SLOTS
                          : limits->stack_slots;
    rt->call_depth = limits == NULL || limits->call_depth == 0
                         ? MAX_FRAMES
                         : limits->call_depth;
    rt->stack = KEST_ARENA_ARRAY(arena, KestValue, rt->stack_slots);
    rt->frames = KEST_ARENA_ARRAY(arena, Frame, rt->call_depth);
    rt->natives = KEST_ARENA_ARRAY(arena, KestNative, module->extern_count + 1);
    rt->contexts = KEST_ARENA_ARRAY(arena, void *, module->extern_count + 1);
    rt->heap = kest_arena_new();
    rt->heap_bytes = limits == NULL ? 0 : limits->heap_bytes;
    if (rt->heap != NULL) {
        kest_arena_cap(rt->heap, rt->heap_bytes);
    }
    if (rt->stack == NULL || rt->frames == NULL || rt->natives == NULL ||
        rt->contexts == NULL || rt->heap == NULL) {
        kest_arena_free(rt->heap);
        return NULL;
    }
    rt->limit = rt->stack + rt->stack_slots;

    // What the program declared against what the host provides, settled by
    // name and reported by name, before anything runs.
    bool unbound = false;
    for (uint32_t i = 0; i < module->extern_count; i++) {
        rt->natives[i] =
            host == NULL ? NULL
                         : kest_host_find(host, module->externs[i].name,
                                          &rt->contexts[i]);
        if (rt->natives[i] == NULL) {
            kest_diags_in(diags, module->externs[i].source);
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0606",
                           module->externs[i].span,
                           "the host does not provide `%s`",
                           module->externs[i].name);
            unbound = true;
        }
    }
    if (unbound) {
        kest_arena_free(rt->heap);
        return NULL;
    }
    return rt;
}

// Whether the program is in the middle of running, which it is exactly when a
// function the host bound is on the stack. Anything that would take the heap
// or the machine out from under it is refused there. See D073.
static bool is_running(const KestRuntime *runtime) {
    return runtime != NULL && runtime->running_top != NULL;
}

void kest_runtime_free(KestRuntime *runtime) {
    if (runtime == NULL) {
        return;
    }
    if (is_running(runtime)) {
        // The frames and the stack are the machine's own and the program is
        // standing on them. Saying so and doing nothing leaves the heap until
        // the build is freed, which is a leak rather than a read of what was
        // freed.
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0613", nowhere,
                       "the machine cannot be freed while the program is "
                       "running");
        kest_diags_suggest(runtime->diags,
                           "free it after the call it was made for returns");
        return;
    }
    kest_arena_free(runtime->heap);
}

KestDiags *kest_runtime_said(KestRuntime *runtime) {
    return runtime->diags;
}

void kest_allowed(const KestRuntime *runtime, KestLimits *limits) {
    if (runtime == NULL || limits == NULL) {
        return;
    }
    limits->stack_slots = runtime->stack_slots;
    limits->call_depth = runtime->call_depth;
    limits->heap_bytes = runtime->heap_bytes;
}

size_t kest_heap_used(const KestRuntime *runtime) {
    return kest_arena_used(runtime->heap);
}

bool kest_heap_reset(KestRuntime *runtime) {
    if (is_running(runtime)) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0613", nowhere,
                       "the heap cannot be thrown away while the program is "
                       "running");
        kest_diags_suggest(runtime->diags,
                           "what it is holding is on it; reset between calls "
                           "rather than inside one");
        return false;
    }
    KestArena *fresh = kest_arena_new();
    if (fresh == NULL) {
        return false;
    }
    kest_arena_free(runtime->heap);
    runtime->heap = fresh;
    // A new heap is the same heap as far as the host is concerned, so what it
    // was allowed is what it is allowed.
    kest_arena_cap(runtime->heap, runtime->heap_bytes);
    return true;
}

void kest_report(KestRuntime *runtime, FILE *out, KestForm form) {
    if (runtime == NULL || out == NULL) {
        return;
    }
    uint32_t from = runtime->reported > runtime->said_before
                        ? runtime->reported
                        : runtime->said_before;
    if (from >= runtime->diags->count) {
        return;
    }
    // A view of the tail rather than anything taken out, so the whole run is
    // still there afterwards.
    KestDiags tail = *runtime->diags;
    tail.items = runtime->diags->items + from;
    tail.count = runtime->diags->count - from;
    // The count belongs to what is being written and not to the run it came
    // from, because JSON says it out loud.
    tail.error_count = 0;
    for (uint32_t i = 0; i < tail.count; i++) {
        if (tail.items[i].severity == KEST_SEVERITY_ERROR) {
            tail.error_count++;
        }
    }
    if (form == KEST_FORM_JSON) {
        kest_diags_render_json(&tail, out);
    } else {
        kest_diags_render(&tail, out);
    }
    runtime->reported = runtime->diags->count;
}

// Why a name did not answer, when the program has heard of it. A name nothing
// knows is a question a host is allowed to ask and gets no answer beyond -1;
// these two are the ones where the program has the name and cannot hand over a
// function, and a host reading -1 would otherwise go looking for a typo.
static bool explain_entry(KestRuntime *runtime, const char *name) {
    const KestModule *module = runtime->module;
    for (uint32_t i = 0; i < module->extern_count; i++) {
        if (strcmp(module->externs[i].name, name) != 0) {
            continue;
        }
        kest_diags_in(runtime->diags, module->externs[i].source);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0614",
                       module->externs[i].span,
                       "`%s` is a function the program asks the host for", name);
        kest_diags_suggest(runtime->diags,
                           "this one crosses the other way: the host binds it "
                           "and the program calls it");
        return true;
    }

    int32_t copies[4];
    uint32_t count = kest_module_copies(module, name, copies, 4);
    if (count < 2) {
        return false;
    }
    char list[192];
    size_t at = 0;
    for (uint32_t i = 0; i < count && i < 4; i++) {
        int wrote = snprintf(list + at, sizeof(list) - at, "%s`%s`",
                             at == 0 ? "" : ", ",
                             module->functions[copies[i]]->name);
        if (wrote < 0 || (size_t)wrote >= sizeof(list) - at) {
            break;
        }
        at += (size_t)wrote;
    }
    KestSpan nowhere = {0, 0};
    kest_diags_in(runtime->diags, NULL);
    kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0615", nowhere,
                   "`%s` is generic and is compiled once for each set of types "
                   "it is used with",
                   name);
    kest_diags_suggest(runtime->diags, "ask for one of them: %s%s", list,
                       count > 4 ? ", and more" : "");
    return true;
}

int32_t kest_entry(KestRuntime *runtime, const char *name) {
    int32_t found = kest_module_find(runtime->module, name);
    if (found >= 0) {
        return found;
    }
    // A host writes what the file writes. The file that was named registered
    // its own names under itself, and nothing about that is the host's
    // business.
    const char *alias = runtime->module->alias;
    size_t prefix = strlen(alias);
    char qualified[256];
    bool composed = prefix != 0 && prefix + strlen(name) + 2 <= sizeof(qualified);
    if (composed) {
        memcpy(qualified, alias, prefix);
        qualified[prefix] = '.';
        memcpy(qualified + prefix + 1, name, strlen(name) + 1);
        found = kest_module_find(runtime->module, qualified);
        if (found >= 0) {
            return found;
        }
    }
    if (!explain_entry(runtime, name) && composed) {
        explain_entry(runtime, qualified);
    }
    return -1;
}

uint32_t kest_frame_slots(KestRuntime *runtime, int32_t entry) {
    if (entry < 0 || (uint32_t)entry >= runtime->module->count) {
        // Zero is also the honest width of a function that takes nothing and
        // gives nothing, so the number cannot say which of the two this is and
        // the report does.
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0616", nowhere,
                       "there is nothing at %d to ask the width of", entry);
        kest_diags_suggest(runtime->diags,
                           "`kest_entry` gives -1 for a name the program does "
                           "not define, and this answers 0 for it as it does "
                           "for a function that takes and gives nothing");
        return 0;
    }
    const KestChunk *chunk = runtime->module->functions[entry];
    // The arguments and the result are the same slots, so a frame has to be
    // wide enough for whichever is wider.
    return chunk->param_slots > chunk->result_slots ? chunk->param_slots
                                                    : chunk->result_slots;
}

bool kest_call(KestRuntime *runtime, int32_t entry, KestValue *frame,
               uint32_t slots) {
    KestSpan nowhere = {0, 0};
    if (entry < 0 || (uint32_t)entry >= runtime->module->count) {
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0607", nowhere,
                       "there is nothing at %d to call", entry);
        kest_diags_suggest(runtime->diags,
                           "`kest_entry` gives -1 for a name the program does "
                           "not define");
        return false;
    }
    // No frame is a frame of no slots, and the checks below are what makes
    // that true: a function that takes anything is refused, so nothing further
    // down has a null to guard against. It used to be permission to skip them,
    // and a call with no frame ran on whatever the stack floor was still
    // holding and said it had worked.
    if (frame == NULL) {
        slots = 0;
    }

    int32_t index = entry;
    // What a program writes, rather than what it was compiled under: a
    // function is registered with what it takes in its name and a host never
    // wrote that down.
    char written[128];
    const char *symbol = runtime->module->functions[index]->name;
    const char *hash = strchr(symbol, '#');
    size_t plain = hash == NULL ? strlen(symbol) : (size_t)(hash - symbol);
    if (plain >= sizeof(written)) {
        plain = sizeof(written) - 1;
    }
    memcpy(written, symbol, plain);
    written[plain] = '\0';
    const char *name = written;

    const KestChunk *chunk = runtime->module->functions[index];
    // What the program takes is not something a host can be trusted about:
    // the arguments go into the frame and the result comes back over them, so
    // a frame that is too narrow is read past on the way in and written past
    // on the way out.
    if (slots < chunk->param_slots) {
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0611", nowhere,
                       "`%s` takes %u slot%s and this frame holds %u", name,
                       chunk->param_slots, chunk->param_slots == 1 ? "" : "s",
                       slots);
        kest_diags_suggest(runtime->diags,
                           "`kest_frame_slots` says how wide it has to be");
        return false;
    }
    // And what comes back is known before anything runs: a `return` never
    // gives back more than the declaration says, which `kest_module_prove`
    // holds the emitted code to. Refusing here rather than afterwards is a
    // program that has not done whatever it does and had its answer thrown
    // away for a frame it could have been told about first.
    if (slots < chunk->result_slots) {
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0611", nowhere,
                       "`%s` gives %u slot%s back and this frame holds %u",
                       name, chunk->result_slots,
                       chunk->result_slots == 1 ? "" : "s", slots);
        kest_diags_suggest(runtime->diags,
                           "`kest_frame_slots` says how wide it has to be");
        return false;
    }

    // The arguments go where the callee's slots are, which is where its result
    // will be, which is where the caller's frame already holds them.
    KestValue *floor = runtime->running_top != NULL ? runtime->running_top
                                                    : runtime->stack;
    if (chunk->param_slots > 0) {
        memcpy(floor, frame, sizeof(KestValue) * chunk->param_slots);
    }

    uint16_t returned = 0;
    if (!execute(runtime, index, chunk->param_slots, &returned)) {
        return false;
    }
    if (returned > 0) {
        memcpy(frame, floor, sizeof(KestValue) * returned);
    }
    return true;
}
