#include "vm.h"

#include <stdarg.h>
#include <string.h>

#define STACK_SLOTS 65536
#define MAX_FRAMES 1024

// An array is a length and a run of elements. What frees it is not decided:
// the block comes from an arena that lives as long as the program runs, which
// is enough to run one and is not a memory model.
typedef struct {
    uint32_t length;
    uint16_t stride;
    KestValue elements[];
} Array;

typedef struct {
    const KestChunk *chunk;
    const uint8_t *ip;
    // Where this call's slots begin. The operand stack sits above them.
    KestValue *base;
} Frame;

typedef struct {
    const KestSource *source;
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
} Vm;

// The instruction being executed, so a failure is reported at the source it
// came from rather than at the byte after it.
static void fail(Vm *vm, const Frame *frame, const uint8_t *instruction,
                 const char *code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    char message[512];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    uint32_t offset = (uint32_t)(instruction - frame->chunk->code);
    KestSpan span = {frame->chunk->origins[offset], 1};
    kest_diags_add(vm->diags, KEST_SEVERITY_ERROR, code, span, "%s", message);
}

bool kest_vm_run(KestArena *arena, const KestModule *module,
                 const KestSource *source, KestDiags *diags,
                 int64_t *exit_code) {
    *exit_code = 0;

    int32_t entry = kest_module_find(module, "main");
    if (entry < 0) {
        KestSpan nowhere = {0, 0};
        kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0603", nowhere,
                       "this file has no `main` to run");
        kest_diags_suggest(diags, "add `fn main() { }`");
        return false;
    }

    Vm vm = {0};
    vm.source = source;
    vm.diags = diags;
    vm.stack = KEST_ARENA_ARRAY(arena, KestValue, STACK_SLOTS);
    vm.frames = KEST_ARENA_ARRAY(arena, Frame, MAX_FRAMES);
    vm.heap = kest_arena_new();
    if (vm.stack == NULL || vm.frames == NULL || vm.heap == NULL) {
        kest_arena_free(vm.heap);
        return false;
    }
    vm.limit = vm.stack + STACK_SLOTS;

    const KestChunk *chunk = module->functions[entry];
    Frame *frame = &vm.frames[vm.frame_count++];
    frame->chunk = chunk;
    frame->ip = chunk->code;
    frame->base = vm.stack;

    KestValue *top = vm.stack + chunk->slot_count;

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
            uint16_t stride = READ_U16();
            size_t bytes = sizeof(Array) + sizeof(KestValue) * count * stride;
            Array *array = kest_arena_alloc(vm.heap, bytes, 16);
            if (array == NULL) {
                fail(&vm, frame, instruction, "K0605", "out of memory");
                kest_arena_free(vm.heap);
                return false;
            }
            array->length = count;
            array->stride = stride;
            top -= (size_t)count * stride;
            memcpy(array->elements, top,
                   sizeof(KestValue) * count * stride);
            (top++)->object = array;
            break;
        }
        case KEST_OP_INDEX: {
            uint16_t stride = READ_U16();
            int64_t index = (--top)->integer;
            const Array *array = (--top)->object;
            if (index < 0 || (uint64_t)index >= array->length) {
                fail(&vm, frame, instruction, "K0604",
                     "index %lld is outside an array of length %u",
                     (long long)index, array->length);
                kest_arena_free(vm.heap);
                return false;
            }
            memcpy(top, array->elements + (size_t)index * stride,
                   sizeof(KestValue) * stride);
            top += stride;
            break;
        }
        case KEST_OP_ELEM_ADDR: {
            uint16_t stride = READ_U16();
            int64_t index = (--top)->integer;
            Array *array = (--top)->object;
            if (index < 0 || (uint64_t)index >= array->length) {
                fail(&vm, frame, instruction, "K0604",
                     "index %lld is outside an array of length %u",
                     (long long)index, array->length);
                kest_arena_free(vm.heap);
                return false;
            }
            (top++)->object = array->elements + (size_t)index * stride;
            break;
        }
        case KEST_OP_LOAD_AT: {
            uint16_t offset = READ_U16();
            uint16_t size = READ_U16();
            const KestValue *at = (--top)->object;
            memcpy(top, at + offset, sizeof(KestValue) * size);
            top += size;
            break;
        }
        case KEST_OP_STORE_AT: {
            uint16_t offset = READ_U16();
            uint16_t size = READ_U16();
            top -= size;
            KestValue *value = top;
            KestValue *at = (--top)->object;
            memmove(at + offset, value, sizeof(KestValue) * size);
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
                fail(&vm, frame, instruction, "K0601", "division by zero");
                kest_arena_free(vm.heap);
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
                fail(&vm, frame, instruction, "K0601", "division by zero");
                kest_arena_free(vm.heap);
                return false;
            }
            uint64_t a = (uint64_t)left.integer;
            uint64_t b = (uint64_t)right.integer;
            (top++)->integer =
                (int64_t)(instruction[0] == KEST_OP_DIV_U ? a / b : a % b);
            break;
        }
        case KEST_OP_NEG_I:
            top[-1].integer = -top[-1].integer;
            break;

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

            if (vm.frame_count == MAX_FRAMES) {
                fail(&vm, frame, instruction, "K0602",
                     "calls nest more than %d deep", MAX_FRAMES);
                kest_arena_free(vm.heap);
                return false;
            }
            KestValue *base = top - argument_slots;
            if (base + callee->slot_count + callee->stack_needed > vm.limit) {
                fail(&vm, frame, instruction, "K0602", "out of stack");
                kest_arena_free(vm.heap);
                return false;
            }

            frame = &vm.frames[vm.frame_count++];
            frame->chunk = callee;
            frame->ip = callee->code;
            frame->base = base;
            top = base + callee->slot_count;
            break;
        }

        case KEST_OP_PRINT:
            fputs((--top)->text, stdout);
            fputc('\n', stdout);
            break;

        case KEST_OP_RETURN: {
            uint16_t count = READ_U16();
            // The result lands where the arguments were, which is where the
            // caller left room for it.
            KestValue *base = frame->base;
            memmove(base, top - count, sizeof(KestValue) * count);

            vm.frame_count--;
            if (vm.frame_count == 0) {
                *exit_code = count > 0 ? base[0].integer : 0;
                kest_arena_free(vm.heap);
                return true;
            }
            frame = &vm.frames[vm.frame_count - 1];
            top = base + count;
            break;
        }
        }
    }

#undef READ_BYTE
#undef READ_U16
#undef BINARY_I
}
