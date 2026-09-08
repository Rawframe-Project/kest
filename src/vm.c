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
typedef struct {
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
                   const unsigned char *from) {
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
    // The frames live in the arena rather than on the host's stack, so the
    // depth limit is Kest's own number and not whatever the host allows.
    Frame *frames;
    uint32_t frame_count;
    uint32_t stack_slots;
    uint32_t call_depth;
};

typedef struct KestRuntime Vm;

KestValue kest_borrow(KestRuntime *runtime, void *data, uint32_t length,
                      uint16_t stride) {
    KestValue value = {0};
    Array *array = kest_arena_alloc(runtime->heap, sizeof(Array), 16);
    if (array == NULL) {
        return value;
    }
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
// The name a program writes for a type, which is the last piece of the one it
// is registered under: a type declared in `examples.flags` is `State` there,
// and that is where it is usually printed.
static const char *written_name(const KestType *type) {
    const char *dot = strrchr(type->name, '.');
    return dot == NULL ? type->name : dot + 1;
}

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
    const char *named = written_name(set);
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
        const char *named = written_name(type);
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
    default:
        return put_text(out, room, "?");
    }
}

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
    rt->frame_count = 0;
    Frame *frame = &rt->frames[rt->frame_count++];
    frame->chunk = chunk;
    frame->ip = chunk->code;
    frame->base = rt->stack;

    KestValue *top = rt->stack + (chunk->slot_count > arg_slots
                                      ? chunk->slot_count
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
                fail(vmp, frame, instruction, "K0605", "out of memory");
                return false;
            }
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
                fail(vmp, frame, instruction, "K0605", "out of memory");
                return false;
            }
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
                    fail(vmp, frame, instruction, "K0605", "out of memory");
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
            if (index < 0 || (uint64_t)index >= array->length) {
                fail(vmp, frame, instruction, "K0604",
                     "index %lld is outside an array of length %u",
                     (long long)index, array->length);
                return false;
            }
            (top++)->object = array->bytes + (size_t)index * array->stride;
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
                fail(vmp, frame, instruction, "K0605", "out of memory");
                return false;
            }
            store->stride = READ_U16();
            (top++)->object = store;
            break;
        }
        case KEST_OP_ADD: {
            uint16_t stride = READ_U16();
            top -= stride;
            KestValue *value = top;
            Store *store = (--top)->object;

            uint32_t index;
            if (store->free_count > 0) {
                index = store->free_slots[--store->free_count];
            } else {
                if (store->used == store->capacity &&
                    !grow_store(rt->heap, store)) {
                    fail(vmp, frame, instruction, "K0605", "out of memory");
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
        case KEST_OP_SEEK: {
            int64_t from = (--top)->integer;
            const Store *store = (--top)->object;
            int64_t found = -1;
            for (uint32_t i = from < 0 ? 0 : (uint32_t)from; i < store->used;
                 i++) {
                if (store->live[i]) {
                    found = i;
                    break;
                }
            }
            (top++)->integer = found;
            break;
        }
        case KEST_OP_STORE_REF: {
            uint32_t index = (uint32_t)(--top)->integer;
            const Store *store = (--top)->object;
            (top++)->integer = pack_ref(store->generations[index], index);
            break;
        }
        case KEST_OP_COUNT: {
            const Store *store = top[-1].object;
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
                fail(vmp, frame, instruction, "K0605", "out of memory");
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
                fail(vmp, frame, instruction, "K0605", "out of memory");
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
                fail(vmp, frame, instruction, "K0605", "out of memory");
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
            char *text = kest_arena_alloc(rt->heap, bytes->length + 1, 1);
            if (text == NULL) {
                fail(vmp, frame, instruction, "K0605", "out of memory");
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
                fail(vmp, frame, instruction, "K0605", "out of memory");
                return false;
            }
            memcpy(piece, text + from, (size_t)count);
            piece[count] = '\0';
            (top++)->text = piece;
            break;
        }
        case KEST_OP_TEXT_FIND: {
            const char *needle = (--top)->text;
            const char *haystack = (--top)->text;
            const char *at = strstr(haystack, needle);
            (top++)->integer = at == NULL ? 0 : (int64_t)(at - haystack);
            (top++)->integer = at != NULL;
            break;
        }
        case KEST_OP_LEN: {
            const Array *array = top[-1].object;
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

        case KEST_OP_CALL_HOST: {
            uint16_t index = READ_U16();
            uint16_t argument_slots = READ_U16();
            uint16_t result_slots = READ_U16();
            KestValue *base = top - argument_slots;
            // The same convention a Kest call uses: the arguments are where
            // the result goes.
            natives[index](base, rt, rt->contexts[index]);
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
            if (rt->frame_count == 0) {
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

void kest_runtime_free(KestRuntime *runtime) {
    if (runtime != NULL) {
        kest_arena_free(runtime->heap);
    }
}

size_t kest_heap_used(const KestRuntime *runtime) {
    return kest_arena_used(runtime->heap);
}

bool kest_heap_reset(KestRuntime *runtime) {
    KestArena *fresh = kest_arena_new();
    if (fresh == NULL) {
        return false;
    }
    kest_arena_free(runtime->heap);
    runtime->heap = fresh;
    return true;
}

bool kest_defines(const KestRuntime *runtime, const char *name) {
    return kest_module_find(runtime->module, name) >= 0;
}

bool kest_call(KestRuntime *runtime, const char *name, KestValue *frame) {
    int32_t index = kest_module_find(runtime->module, name);
    if (index < 0) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0607", nowhere,
                       "this program has no `%s` to call", name);
        return false;
    }

    const KestChunk *chunk = runtime->module->functions[index];
    // The arguments go where the callee's slots are, which is where its result
    // will be, which is where the caller's frame already holds them.
    if (frame != NULL && chunk->param_slots > 0) {
        memcpy(runtime->stack, frame, sizeof(KestValue) * chunk->param_slots);
    }

    uint16_t returned = 0;
    if (!execute(runtime, index, chunk->param_slots, &returned)) {
        return false;
    }
    if (frame != NULL && returned > 0) {
        memcpy(frame, runtime->stack, sizeof(KestValue) * returned);
    }
    return true;
}

bool kest_vm_run(KestArena *arena, const KestModule *module,
                 const char *entry_name, const KestHost *host,
                 KestDiags *diags, int64_t *exit_code) {
    *exit_code = 0;

    if (kest_module_find(module, entry_name) < 0) {
        KestSpan nowhere = {0, 0};
        kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0603", nowhere,
                       "this file has no `main` to run");
        kest_diags_suggest(diags, "add `fn main() { }`");
        return false;
    }

    KestRuntime *rt = kest_runtime_new(arena, module, host, diags, NULL);
    if (rt == NULL) {
        return false;
    }

    KestValue frame[1] = {{0}};
    bool ran = kest_call(rt, entry_name, frame);
    if (ran) {
        const KestChunk *chunk =
            module->functions[kest_module_find(module, entry_name)];
        *exit_code = chunk->returns_value ? frame[0].integer : 0;
    }
    kest_runtime_free(rt);
    return ran;
}
