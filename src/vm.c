#include "vm.h"

// The sanitised build is told where every block a host has ends, which is the
// one thing a library cannot work out for itself about somebody else's memory.
#if KEST_CHECKED
#include <sanitizer/asan_interface.h>
#endif

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// What a host gets when it says nothing.
// The two a host gets by saying nothing, which `kest.h` names so that a host
// can say the same thing on purpose.
#define STACK_SLOTS KEST_STACK_SLOTS
#define MAX_FRAMES KEST_CALL_DEPTH

// How many of a thing the program can be told it has. `len` gives back an
// `i32`, so this is one number and not three: an array, a store and a text
// are counted by the same builtin and stop at the same place.
#define MAX_COUNTED INT32_MAX

// An array is a length and a run of elements laid out the way the host lays
// them out: an array of `f32` is four bytes an element. The block is separate
// from the header so that it can one day be the host's own. What frees it is
// not decided; see D012.
// What a handle is. A host holds these as opaque values and can hand one back
// where another was wanted, which nothing at the boundary can see: the machine
// carries no types and D046 says why. So the handle says what it is.
// How many places in stores a machine can tell apart. A reference carries the
// stamp its slot was handed out with and a stamp is a `u32`, so this is where
// they run out — four thousand million of them, after which a stamp handed out
// again would make a reference from the first occupant read as the newest one,
// which is the one thing a reference is for.
#define MOST_STAMPS 0xffffffffu

#define KEST_IS_ARRAY 0x4b415252u
#define KEST_IS_STORE 0x4b53544fu
// What a lend the host has taken back is. It is not any of the others, so it
// is refused wherever a handle is used, and it is not nothing either: the
// program is told what happened to it rather than told it never was one.
#define KEST_WAS_LENT 0x4b454e44u

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
    // How far a walk goes, and how far the counts have been written. They are
    // the same until a store is emptied: a walk over a store that held a
    // million and holds none would step over a million dead slots, so the
    // extent goes back to nought there. What each slot has counted stays,
    // because that is what makes a reference from before stale, so `high` is
    // where a slot has never been used at all and needs its first count.
    uint32_t used;
    uint32_t high;
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
    // What this machine is made of, in an arena of its own: the stack, the
    // frames, the table of what the host provides, and this struct. It goes
    // when the machine goes, rather than sitting on the build's arena until
    // the build does — a host that starts a machine, frees it and starts
    // another was paying for every one of them. See D574.
    KestArena *own;
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
    // And whether this machine has said the one thing a run with no memory
    // can say, which is not in the list because making a list entry is what
    // there was no room for.
    bool starve_said;
    // Where the machine is while a host function it called is running. A host
    // may call back in from there, and what it starts has to stand above what
    // is already on the stack rather than on top of it. NULL when nothing of
    // the program's is running. See D072.
    KestValue *running_top;
    uint32_t running_frames;
    // Headers of lends the host has ended, kept to be lent again. A host that
    // lends a batch a frame and ends it at the end of the frame would
    // otherwise leave a header on the heap every frame, which is a frame
    // budget that grows for a program that does the same thing every time.
    // The link is the block pointer, which an ended lend has no use for.
    // See D241.
    Array *spare_lends;
    // Every lend the host has not ended, so that ending one ends every handle
    // over that block: a host lending the same memory twice has two handles
    // and one block, and it is the block it takes back. See D283.
    // What a place is stamped with when it is handed out, which is the
    // build's: no two places in any two stores of any two machines from one
    // build are ever stamped the same. A reference carries the stamp it was
    // made with, so one handed to a store it did not come from names a place
    // stamped by something else. See D314 and D316.
    uint32_t *stamps;
    // The build's count of what is standing on it, which this machine is one
    // of until it is freed.
    uint32_t *standing;
    Array **lent;
    uint32_t lent_count;
    uint32_t lent_capacity;
    // What a host was told it needs where the program calls into it, which is
    // what `kest_needs_from` answers and a host sizes a stack from. Held
    // against what the machine turns out to be there. False when the program
    // reaches itself or calls through a value, and then there was no number
    // to give a host and none to hold. See D234.
    // Which of the names a machine can explain it has explained already. What
    // a host asked for and cannot call is a statement about the program, which
    // does not change while a machine lives — it is not something that
    // happened — and saying it again cost 634 bytes an asking, which a host
    // polling for a name it might have pays every frame. One byte a name, and
    // the program's own count of them. See D608.
    uint8_t *said_extern;
    uint8_t *said_copy;
    // And which of the program's layouts it has said something about. A
    // refusal about a lend that is about the program — how wide the program
    // lays a type out, that two of a name are in it, that one holds the
    // machine's own — says the same thing every time it is asked, and a host
    // lending in a frame asks it every frame. One byte a layout, which is
    // what a lend names. See D616.
    uint8_t *said_layout;
    // Where this machine's own room stood when it had been built, and when
    // what it said was last read. What a host has been told is the host's,
    // and the words it was told go back to the machine that wrote them: a
    // program refused every frame says the same sentence every frame, and a
    // machine that kept all of them would hold a frame's words for as long as
    // it ran. See D617.
    KestMark after_said;
    // And whether a host has been told what an index that is no function is.
    // The number in it is how many functions the program has, which does not
    // change; `kest_entry_name` answers the same question for nothing. One
    // bit, because there is one sentence and it is about the program rather
    // than about an index. See D615.
    bool said_no_frame;
    bool host_measured;
    uint32_t host_slots;
    uint32_t host_frames;
    // And which function that measurement was of: the one holding the deepest
    // call into the host, which the walk names. A fault says both it and the
    // function the call it refused is in, because the two being different is
    // the difference between a walk that measured the wrong chunk and one that
    // measured the right chunk wrongly. It is a name in the module, which
    // outlives this machine. See D606.
    const char *host_where;
};

typedef struct KestRuntime Vm;

// What the program calls a type, which is the last piece of the name it is
// registered under. A host writes `Event`, not the module it came from.
// The program's side of a disagreement about a lend, written the way a
// declaration is, so a host can put it beside its own struct and see which
// field moved. Empty for a type that has no members to write.
// The first few fields of a struct and where each of them sits, for a host
// that has laid the same shape out differently. Four of them, and a count of
// the rest: a reader comparing two declarations has the first disagreement in
// front of them by then.
//
// In the arena and as long as those four are. It was a hundred and ninety-two
// bytes and stopped where they ran out, so a struct with long field names lost
// the rest of the list and the count of what was lost with it.
#define SHOWN_FIELDS 4

static const char *written_shape(KestArena *arena, const KestType *type) {
    uint32_t shown =
        type->member_count < SHOWN_FIELDS ? type->member_count : SHOWN_FIELDS;
    size_t room = strlen(", and 4294967295 more") + 1;
    for (uint32_t i = 0; i < shown; i++) {
        room += strlen(type->members[i].name) +
                strlen(kest_type_name(arena, type->members[i].type)) +
                strlen(", `: ` at 4294967295");
    }
    char *out = kest_arena_alloc(arena, room, 1);
    if (out == NULL) {
        return "";
    }

    out[0] = '\0';
    size_t at = 0;
    for (uint32_t i = 0; i < shown; i++) {
        const KestMember *member = &type->members[i];
        at += (size_t)snprintf(out + at, room - at, "%s`%s: %s` at %u",
                               at == 0 ? "" : ", ", member->name,
                               kest_type_name(arena, member->type),
                               member->byte_offset);
    }
    if (type->member_count > shown) {
        snprintf(out + at, room - at, ", and %u more",
                 type->member_count - shown);
    }
    return out;
}

// Where a type was written, when it was written anywhere: a note points at the
// declaration the host has come apart from, which is the half of the
// disagreement the host cannot see.
// Whether this host has already been told something about this layout. What a
// refusal about a lend says of the program — how wide it lays a type out, that
// two of a name are in it, that one of them holds the machine's own — is true
// of the program and says the same thing every time it is asked. A host that
// lends in a frame asks every frame, and what saying it again cost was 749
// bytes an asking, on the arena the build's diagnostics are written in and
// never handed back. See D616.
//
// A layout the module does not hold is not one of these: it is said, because
// nothing here knows what it is about.
static bool told_about(KestRuntime *runtime, const KestLayout *layout) {
    if (layout == NULL || runtime->module->layouts == NULL) {
        return false;
    }
    size_t which = (size_t)(layout - runtime->module->layouts);
    if (which >= runtime->module->layout_count) {
        return false;
    }
    if (runtime->said_layout[which] != 0) {
        return true;
    }
    runtime->said_layout[which] = 1;
    return false;
}

static void note_declaration(KestRuntime *runtime, const KestLayout *layout,
                             const char *label) {
    const KestType *type = layout->type;
    if (type == NULL || type->declared_in == NULL) {
        return;
    }
    kest_diags_note(runtime->diags, type->declared_in, type->span, "%s", label);
}

KestValue kest_text(KestRuntime *runtime, const char *bytes, uint32_t length) {
    KestValue value = {0};
    value.text = "";
    if (runtime == NULL) {
        return value;
    }
    // Nothing to copy is not the same as nothing to say. What comes back for
    // it is an empty piece of text, which is also what comes back for a host
    // that handed over an empty one on purpose, so the two read alike and
    // only this says which. See D436.
    if (bytes == NULL) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0611", nowhere,
                       "this host handed over %u byte%s of text and no address "
                       "to find them at",
                       length, length == 1 ? "" : "s");
        kest_diags_suggest(runtime->diags,
                           "a host with nothing to say hands over an empty "
                           "piece of text; an address of nothing is bytes that "
                           "were never there");
        return value;
    }
    // Copied into the machine's heap, which is what the program's own text is
    // in: a host that handed a pointer of its own would be promising to keep
    // it as long as the program holds it, and a program holds a piece of text
    // for as long as it likes.
    for (uint32_t i = 0; i < length; i++) {
        if (bytes[i] == 0) {
            KestSpan nowhere = {0, 0};
            kest_diags_in(runtime->diags, NULL);
            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0611",
                           nowhere,
                           "byte %u of what the host handed over is zero, and "
                           "text ends at a zero byte",
                           i);
            return value;
        }
    }
    char *held = kest_arena_alloc(runtime->heap, length + 1, 1);
    if (held == NULL) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0605", nowhere,
                       "out of memory");
        return value;
    }
    memcpy(held, bytes, length);
    held[length] = '\0';
    value.text = held;
    return value;
}

// A lend that could not be written down. What it costs is a header and a place
// in the list of what is lent, both on the machine's heap, so a host that lends
// without ending pays for every one of them until the heap goes — and the heap
// that runs out here is the one the host itself gave. Nothing about the value
// says which of the reasons in this function it was: a `NULL` for a name that
// is not there and a `NULL` for a heap with nothing left were the same answer,
// and only one of them is about the program. See D350.
static void no_room_to_lend(KestRuntime *runtime) {
    KestSpan nowhere = {0, 0};
    kest_diags_in(runtime->diags, NULL);
    if (runtime->heap_bytes != 0) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0643", nowhere,
                       "this host lent something and the heap it gave has "
                       "%zu of its %zu bytes left",
                       runtime->heap_bytes - kest_heap_used(runtime),
                       runtime->heap_bytes);
    } else {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0643", nowhere,
                       "this host lent something and this machine has no room "
                       "to write it down");
    }
    kest_diags_suggest(runtime->diags,
                       "a lend costs a header and a place in the list of what "
                       "is lent: end the ones this host is done with, or give "
                       "the machine more heap");
}

KestValue kest_borrow(KestRuntime *runtime, void *data, uint32_t length,
                      const char *element, size_t size) {
    KestValue value = {0};

    // A lend is an address and a count, and a host with nothing to lend has a
    // count of nought rather than an address of nothing. What used to happen
    // was that the program got an array of four bytes at no address and read
    // it: the machine cannot tell a bad address from a good one, and this is
    // the one address it can. See D357.
    KestSpan missing = {0, 0};
    if (data == NULL && length > 0) {
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0644", missing,
                       "this host lent %u `%s` and gave no address to find "
                       "them at",
                       length, element);
        kest_diags_suggest(runtime->diags,
                           "a host with nothing to lend lends nought of them; "
                           "an address of nothing is a block that was never "
                           "there");
        return value;
    }

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
        if (!told_about(runtime, named_layouts[0])) {
            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0610",
                           nowhere, "more than one `%s` is in this program",
                           element);
            for (uint32_t i = 0; i < 2; i++) {
                note_declaration(runtime, named_layouts[i], "this one");
            }
            const char *askable = kest_module_askable(runtime->module, element);
            kest_diags_suggest(runtime->diags,
                               "write the module it came from: `%s`",
                               askable == NULL ? "world.Event" : askable);
        }
        return value;
    }
    // And what is inside it, which is the one thing about a lend that no
    // number can say. The bytes are the host's: a pointer in them is one the
    // machine did not put there, cannot vouch for, and cannot take back when
    // the lend ends — so a program reading it would be holding the host's
    // memory after the host had moved on. `text` of a lent run is the one
    // place a lend stops being free, and this is what keeps it the one.
    const KestType *own = NULL;
    if (layout->type != NULL && kest_type_holds_own(layout->type, &own)) {
        if (!told_about(runtime, layout)) {
            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0647",
                           nowhere,
                           "`%s` holds `%s`, which is the machine's own and "
                           "cannot be lent",
                           element, kest_type_name(runtime->diags->arena, own));
            note_declaration(runtime, layout, "this is what it holds");
            kest_diags_suggest(runtime->diags,
                               "lend the numbers and hand the rest over a "
                               "frame: `kest_text` makes text the machine "
                               "keeps, and what a program keeps of a lend is "
                               "what it copied out of one");
        }
        return value;
    }

    uint16_t stride = layout->size;
    if (size != stride) {
        if (told_about(runtime, layout)) {
            return value;
        }
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0610", nowhere,
                       "the program lays `%s` out in %u bytes and this host "
                       "has %zu",
                       element, stride, size);
        const KestType *type = layout->type;
        const char *shape = type != NULL && type->member_count > 0
                                ? written_shape(runtime->diags->arena, type)
                                : "";
        note_declaration(runtime, layout,
                         shape[0] == '\0' ? "this is what it lays out" : shape);
        kest_diags_suggest(runtime->diags,
                           "the two declarations have come apart");
        return value;
    }

    // Where the host put it. The size says how far apart two of them are and
    // the pieces say what is inside one; neither says the address is one the
    // program may read a field from. It is the only thing about a lend that
    // nothing in the program can be written to get wrong, and the only one
    // the host alone knows.
    uint16_t align = layout->align == 0 ? 1 : layout->align;
    uintptr_t past = (uintptr_t)data % align;
    if (past != 0) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0610", nowhere,
                       "the program aligns `%s` to %u bytes and this host lent "
                       "one %u past a multiple of that",
                       element, align, (unsigned)past);
        note_declaration(runtime, layout, "this is the type it is about");
        kest_diags_suggest(runtime->diags,
                           "lend an array of the type itself, which the host's "
                           "own compiler aligns; a byte buffer read as one is "
                           "not aligned by anything");
        return value;
    }

    // What can be said in either build is what the program is able to count
    // to, and it is asked first for exactly that reason: a count no `i32`
    // holds is wrong whatever the host owns, and one lend that got two
    // different answers in two builds would be a message a reader could not
    // repeat. See D358. `len` gives back an `i32`, so a lend longer than one holds is a lend
    // whose end the program cannot see, and every loop over it walks off
    // memory that is really there into memory that is not.
    if (length > MAX_COUNTED) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0610", nowhere,
                       "this host lent %u `%s` and the program counts them "
                       "with an `i32`",
                       length, element);
        note_declaration(runtime, layout, "this is the type it is about");
        kest_diags_suggest(runtime->diags,
                           "lend %d at a time at the most; `len` is where the "
                           "program reads the end from",
                           MAX_COUNTED);
        return value;
    }

    // How many there are is the host's word and nothing here can weigh it in a
    // build that ships: the memory is the host's and its end is not written
    // down anywhere the library can read. The sanitised build can ask, because
    // it is told where every block a host has ends, and a lend that runs past
    // one is the mistake this crossing is shaped around — every loop over it
    // walks off memory that is really there into memory that is not. See D286.
#if KEST_CHECKED
    if (length > 0 &&
        __asan_region_is_poisoned(data, (size_t)length * stride) != NULL) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0610", nowhere,
                       "this host lent %u `%s` and does not own that many",
                       length, element);
        note_declaration(runtime, layout, "this is the type it is about");
        kest_diags_suggest(runtime->diags,
                           "the count is the host's word and this build "
                           "weighs it: lend what is there");
        return value;
    }
#endif

    // One the host ended, if there is one, and a new one otherwise. What is
    // reused is the header and never the block: the block is the host's and
    // this one is the one just handed over.
    Array *array = runtime->spare_lends;
    if (array != NULL) {
        runtime->spare_lends = (Array *)(void *)array->bytes;
    } else {
        array = kest_arena_alloc(runtime->heap, sizeof(Array), 16);
    }
    if (array == NULL) {
        no_room_to_lend(runtime);
        return value;
    }
    // Written down before it is handed over: a lend nothing knows about is one
    // that cannot be taken back with the rest of its block.
    if (runtime->lent_count == runtime->lent_capacity) {
        uint32_t bigger = runtime->lent_capacity == 0
                              ? 8
                              : runtime->lent_capacity * 2;
        // On the heap, where the headers are: a lend costs a header and a
        // place in this, and a heap thrown away takes both with it.
        Array **grown = KEST_ARENA_ARRAY(runtime->heap, Array *, bigger);
        if (grown == NULL) {
            no_room_to_lend(runtime);
            return value;
        }
        for (uint32_t i = 0; i < runtime->lent_count; i++) {
            grown[i] = runtime->lent[i];
        }
        runtime->lent = grown;
        runtime->lent_capacity = bigger;
    }
    runtime->lent[runtime->lent_count++] = array;

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

// Only this file writes a value now: what a host asks is `kest_gave_text`,
// and the command line is a host.
static size_t kest_write_value(char *out, size_t room, const KestType *type,
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
    // One slot with a number in it, which is what these three are and the only
    // thing there is to compare about them.
    case KEST_T_INT:
    case KEST_T_BOOL:
    case KEST_T_FLAGS:
        return a[0].integer == b[0].integer;
    // Every other tag written out rather than left to a `default`, for the
    // reason `hash_value` beside it gives: what reaches this is decided by
    // `has_equality`, the two lists are held to being one another, and a tag
    // added to the language would otherwise compare by slot nought without
    // anybody deciding it should. See D545.
    case KEST_T_ERROR:
    case KEST_T_VOID:
    case KEST_T_OPTIONAL:
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
    // Nothing reaches this: the checker asks `has_equality` first and it says
    // no to every tag above. Two values of a type that does not compare are
    // not equal, which is the answer that cannot be mistaken for one.
    return false;
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
    // One slot with a number in it, which is what these three are: a whole
    // number, a truth and a set of bits are the bits in slot nought and
    // nothing else.
    case KEST_T_INT:
    case KEST_T_BOOL:
    case KEST_T_FLAGS:
        return mix((uint64_t)slots[0].integer);
    // Every other tag written out rather than left to a `default`, so that a
    // tag added to the language cannot land here by not being mentioned. What
    // decides which reach this is `has_equality`, which lists the same tags,
    // and the compiler holds the two lists to being one another. See D542.
    case KEST_T_ERROR:
    case KEST_T_VOID:
    case KEST_T_OPTIONAL:
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
    // Nothing reaches this: `hash` is refused for every tag above by the
    // checker, which asks `has_equality` first. It is here because C wants a
    // value, and nought is the one a reader of a fault would rather see than
    // whatever was in slot nought.
    return 0;
}

// A handle that is not what was wanted is a host mistake rather than a
// program one: the machine carries no types, so nothing at the boundary could
// have caught it. It is caught here instead.
// An index is refused where it is used, and seven instructions use one. The
// sentence is here rather than seven times over: an array has a length and
// that many of something has a number, so there are two of these and not one.
#define IN_ARRAY(index, array)                                                 \
    do {                                                                       \
        if ((index) < 0 || (uint64_t)(index) >= (array)->length) {              \
            fail(vmp, frame, instruction, "K0604",                             \
                 "index %lld is outside an array of length %u",                \
                 (long long)(index), (array)->length);                         \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define IN_RUN(index, count)                                                   \
    do {                                                                       \
        if ((index) < 0 || (uint64_t)(index) >= (count)) {                     \
            fail(vmp, frame, instruction, "K0604",                             \
                 "index %lld is outside %u of them", (long long)(index),       \
                 (count));                                                     \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define HOLD(handle, tag, what)                                              \
    do {                                                                     \
        if (!KEST_HANDLE_IS(handle, tag)) {                                  \
            if (KEST_HANDLE_IS(handle, KEST_WAS_LENT)) {                     \
                fail(vmp, frame, instruction, "K0637",                       \
                     "the host has taken this lend back");                   \
                kest_diags_suggest(vmp->diags,                               \
                                   "the block is the host's and it said so; " \
                                   "what a program keeps of a lend is what "  \
                                   "it copied out of one");                   \
                return false;                                                \
            }                                                                \
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
                 const char *code, const char *format, ...) KEST_SAYS(5, 6);

static void fail(Vm *vm, const Frame *frame, const uint8_t *instruction,
                 const char *code, const char *format, ...) {
    uint32_t offset = (uint32_t)(instruction - frame->chunk->code);
    KestSpan span = {frame->chunk->origins[offset], 1};
    kest_diags_in(vm->diags, frame->chunk->source);
    // The file the instruction came from was set when it was compiled, and
    // the machine does not change it.
    va_list args;
    va_start(args, format);
    kest_diags_addv(vm->diags, KEST_SEVERITY_ERROR, code, span, format, args);
    va_end(args);

    // And how it got here. Every frame under this one made a call, and its
    // `ip` is just past the instruction that made it, so the byte before is
    // where that call is written. Outermost first, so the notes read as the
    // way in rather than as the way back out.
    //
    // Eight is what a diagnostic holds; a run of calls deeper than that says
    // how many were left out, because a number is what a reader of a deep one
    // wants and the middle of it is not.
    uint32_t depth = vm->frame_count;
    uint32_t shown = depth > KEST_MOST_PLACES + 1 ? KEST_MOST_PLACES : depth - 1;
    for (uint32_t i = 1; i <= shown && i < depth; i++) {
        const Frame *caller = &vm->frames[i - 1];
        const KestChunk *chunk = caller->chunk;
        if (chunk == NULL || chunk->origins == NULL) {
            continue;
        }
        uint32_t at = (uint32_t)(caller->ip - chunk->code);
        KestSpan call = {chunk->origins[at > 0 ? at - 1 : 0], 1};
        const char *written = vm->frames[i].chunk->wrote;
        if (i == shown && depth - 1 > shown) {
            kest_diags_note(vm->diags, chunk->source, call,
                            "`%s` was called here, and %u more under it",
                            written, depth - 1 - shown);
        } else {
            kest_diags_note(vm->diags, chunk->source, call,
                            "`%s` was called here", written);
        }
    }
}

// An allocation that did not happen. Which of the two it was is the difference
// between a machine that has run out and a host that said this much and no
// more, and only one of those is anybody's mistake.
static void no_room(Vm *vm, const Frame *frame, const uint8_t *instruction,
                    const KestRuntime *rt) {
    if (rt->heap_bytes != 0) {
        // What it has and what it wanted, because a program that missed by
        // eight bytes and one that missed by a megabyte are the same message
        // otherwise, and they are not the same problem.
        fail(vm, frame, instruction, "K0617",
             "the program has used %zu of the %zu bytes it was given, and this "
             "asked for %zu more",
             kest_heap_used(rt), rt->heap_bytes, kest_arena_refused(rt->heap));
        return;
    }
    // And the same two numbers when nobody set a ceiling, because a host
    // reading `out of memory` learns nothing it did not know: whether this is
    // a program that wants a gigabyte or a machine that has a megabyte left is
    // the whole of what it would do about it.
    fail(vm, frame, instruction, "K0605",
         "the program has used %zu bytes and this asked for %zu more, which "
         "this machine has not got",
         kest_heap_used(rt), kest_arena_refused(rt->heap));
}

// And what it was doing when it ran out. What a host raises a ceiling by is not
// what the last allocation asked for: a thing that doubles will ask for the
// double again at the next one. What it needs to know is what was growing and
// how far along it was, which is what this says.
static void no_room_growing(Vm *vm, const Frame *frame,
                            const uint8_t *instruction, const KestRuntime *rt,
                            const char *what, uint32_t held, size_t each,
                            uint32_t growing_to) {
    no_room(vm, frame, instruction, rt);
    kest_diags_suggest(vm->diags,
                       "it was %s holding %u of %zu bytes each, growing to %u",
                       what, held, each, growing_to);
}

// And what it would have needed, said where it ran out. A host picks the two
// numbers a machine is started with, and the only thing that says whether it
// picked well is the program — so the refusal carries the answer rather than
// sending a reader to `kest emit` for it: a number to ask for, or the reason
// there is not one. Worked out here rather than kept anywhere, because this
// happens once, on the way out. See D569.
static void what_it_needed(Vm *vm, const KestRuntime *rt, int32_t called) {
    uint32_t slots = 0;
    uint32_t deep = 0;
    KestReason why = {KEST_REACH_UNASKED, NULL};
    // The working out is memory, and the memory it takes is the program's:
    // a host that carries on after a refusal would be paying for this one out
    // of the heap it gave the program. So it is borrowed and put back — after
    // the words are written, because what a diagnostic is written into is the
    // build's arena and what a reason names is the program's own name for a
    // function, and neither is what this hands back. See D571.
    KestMark before = kest_arena_mark(rt->heap);
    if (kest_module_needs(rt->module, rt->heap, -1, &slots, &deep, NULL, NULL,
                          NULL, &why)) {
        kest_diags_suggest(vm->diags,
                           "this program needs %u slots and %u frames, and "
                           "this machine was given %u and %u",
                           slots, deep, rt->stack_slots, rt->call_depth);
        // And what the call a host made needs on its own, at the declaration
        // of the function it called. The number above is the worst of
        // everything the program defines, so a host sized for the functions it
        // calls is being told to go back to saying nothing — and what it wants
        // is the one it asked for, which is this. See D622.
        const KestChunk *one =
            called >= 0 && (uint32_t)called < rt->module->count
                ? rt->module->functions[called]
                : NULL;
        uint32_t its_slots = 0;
        uint32_t its_deep = 0;
        KestReason of_one = {KEST_REACH_UNASKED, NULL};
        if (one != NULL &&
            kest_module_needs(rt->module, rt->heap, called, &its_slots,
                              &its_deep, NULL, NULL, NULL, &of_one)) {
            kest_diags_note(vm->diags, one->source, one->declared,
                            "calling this needs %u slots and %u frames",
                            its_slots, its_deep);
        }
        kest_arena_rewind(rt->heap, before);
        return;
    }
    // And when the working out itself had nowhere to happen, which is a
    // machine that has spent its heap and then run off its stack: both at
    // once, and the second of them is what this was for. Said as what it is,
    // because `there is no number to ask for` would be this machine's trouble
    // written down as the program's — the number is there and this run cannot
    // reach it. See D570.
    if (why.reach == KEST_REACH_NO_ROOM) {
        kest_diags_suggest(vm->diags,
                           "what this program needs cannot be worked out with "
                           "the heap this machine has left");
        kest_arena_rewind(rt->heap, before);
        return;
    }
    // And when there is no number to ask for, which is a thing to be told
    // rather than a silence: a program that reaches itself or calls through a
    // value has no deepest call, so the host that picked a number was always
    // going to find out here, and this is where it does.
    kest_diags_suggest(vm->diags, "there is no number to ask for: `%s` %s",
                       why.where == NULL ? "something here" : why.where,
                       kest_reach_name(why.reach));
    kest_arena_rewind(rt->heap, before);
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

// Room for that many, which is what growing is and what being told how many
// there will be is. The four runs beside each other are what a slot costs: the
// value, how many times the slot has been used, whether it is live, and the
// list of the ones that are not.
static bool room_for(KestArena *heap, Store *store, uint32_t capacity) {
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

static bool grow_store(KestArena *heap, Store *store) {
    return room_for(heap, store, store->capacity == 0 ? 8
                                                      : store->capacity * 2);
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
        // And what there would have been room in, which is the same answer the
        // two refusals inside a run carry: a host that picked the numbers is
        // told the ones to ask for rather than left to find them. This is the
        // door a host meets first, so it is the one most likely to be met by a
        // host that has not asked at all. See D571.
        what_it_needed(vmp, rt, entry);
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
            IN_RUN(index, count);
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
                // What it was making, because nothing here was growing: a
                // number in the program rather than a ceiling that was nearly
                // enough.
                kest_diags_suggest(vmp->diags,
                                   "it was making an array of %u of %u bytes "
                                   "each",
                                   count, layout->size);
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
                // How many was said by the program rather than written into
                // the instruction, so it is as wide as the program can count.
                kest_diags_suggest(vmp->diags,
                                   "it was making an array of %lld of %u bytes "
                                   "each",
                                   (long long)count, layout->size);
                return false;
            }
            array->what = KEST_IS_ARRAY;
            array->length = (uint32_t)count;
            array->capacity = (uint32_t)count;
            array->stride = layout->size;
            array->bytes = bytes;
            // A fill of nought is what the arena already handed over, so the
            // writing is skipped rather than done twice. Every slot being
            // nought is every byte being nought, whatever the pieces are: a
            // slot is eight bytes of whichever kind it is read as, and nought
            // is nought as a number, as a float, and as a handle. What this
            // buys is the room an array is asked for and does not read, which
            // is `array(n, v)` and `clear` — the reservation this language has
            // instead of a word for one.
            bool nothing = true;
            for (uint16_t i = 0; i < layout->count && nothing; i++) {
                nothing = fill[i].integer == 0;
            }
            if (!nothing) {
                for (int64_t i = 0; i < count; i++) {
                    pack(bytes + (size_t)i * layout->size, layout, fill);
                }
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
            // What a program can be told is what it can count to, and `len`
            // gives back an `i32`. One more than that used to double a
            // capacity past what a `uint32_t` holds, which asked for nought
            // bytes and copied two thousand million into them.
            if (array->length == MAX_COUNTED) {
                fail(vmp, frame, instruction, "K0630",
                     "this array holds %d, which is all `len` can count",
                     MAX_COUNTED);
                return false;
            }
            if (array->length == array->capacity) {
                uint32_t capacity = array->capacity == 0 ? 8
                                                         : array->capacity * 2;
                size_t was = (size_t)array->capacity * layout->size + 1;
                size_t want = (size_t)capacity * layout->size + 1;
                // Bigger where it stands, when nothing has been handed out
                // since this was — which is what a loop filling one array is,
                // and what a loop that also makes text is not. Then there is
                // no copy and no block left behind, and an array built by
                // pushing costs what it holds rather than twice that.
                unsigned char *grown =
                    array->capacity == 0
                        ? NULL
                        : kest_arena_extend(rt->heap, array->bytes, was, want);
                if (grown != NULL) {
                    array->bytes = grown;
                    array->capacity = capacity;
                } else {
                    unsigned char *bytes =
                        kest_arena_alloc(rt->heap, want, 16);
                    if (bytes == NULL) {
                        no_room_growing(vmp, frame, instruction, rt, "an array",
                                        array->length, layout->size, capacity);
                        return false;
                    }
                    if (array->length > 0) {
                        memcpy(bytes, array->bytes,
                               (size_t)array->length * layout->size);
                    }
                    // The handle is the header, and the header is what moved
                    // nothing, so every reference to this array sees the
                    // growth.
                    array->bytes = bytes;
                    array->capacity = capacity;
                }
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
            IN_ARRAY(index, array);
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
            IN_ARRAY(index, array);
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
            IN_ARRAY(index, array);
            (top++)->object = array->bytes + (size_t)index * array->stride;
            break;
        }
        case KEST_OP_LOAD_SLOTS: {
            uint16_t base = READ_U16();
            uint16_t stride = READ_U16();
            uint16_t count = READ_U16();
            int64_t index = (--top)->integer;
            IN_RUN(index, count);
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
            IN_RUN(index, count);
            memcpy(frame->base + base + (size_t)index * stride, value,
                   sizeof(KestValue) * stride);
            break;
        }
        case KEST_OP_OFFSET_ADDR: {
            uint16_t stride = READ_U16();
            uint16_t count = READ_U16();
            int64_t index = (--top)->integer;
            unsigned char *at = (--top)->object;
            IN_RUN(index, count);
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
            int64_t room = (--top)->integer;
            if (room < 0) {
                fail(vmp, frame, instruction, "K0604",
                     "a store cannot have room for %lld", (long long)room);
                return false;
            }
            Store *store = kest_arena_alloc(rt->heap, sizeof(Store), 16);
            if (store == NULL) {
                no_room(vmp, frame, instruction, rt);
                kest_diags_suggest(vmp->diags, "it was making a store");
                return false;
            }
            store->what = KEST_IS_STORE;
            store->stride = READ_U16();
            // Made here rather than at the first `add`, which is the whole of
            // what a count buys: the growth is where the program asked for it
            // instead of in whichever frame filled the last slot.
            if (room > 0 && !room_for(rt->heap, store, (uint32_t)room)) {
                no_room(vmp, frame, instruction, rt);
                kest_diags_suggest(vmp->diags,
                                   "it was making a store with room for %lld",
                                   (long long)room);
                return false;
            }
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
                if (store->used == MAX_COUNTED) {
                    fail(vmp, frame, instruction, "K0630",
                         "this store holds %d, which is all `len` can count",
                         MAX_COUNTED);
                    return false;
                }
                if (store->used == store->capacity &&
                    !grow_store(rt->heap, store)) {
                    // A store grows by four runs at once — what it holds, what
                    // each has counted, which are live and which are free —
                    // so what it was reaching for is wider than one of them.
                    no_room_growing(vmp, frame, instruction, rt, "a store",
                                    store->used,
                                    sizeof(KestValue) * store->stride,
                                    store->capacity == 0 ? 8
                                                         : store->capacity * 2);
                    return false;
                }
                index = store->used++;
                if (index >= store->high) {
                    store->high = index + 1;
                }
            }
            // The next stamp there is. A slot handed out again gets a new one,
            // so a reference made before it was given back names a stamp
            // nothing carries any more; and a store that has never seen this
            // stamp is a store this reference did not come from.
            if (*rt->stamps == MOST_STAMPS) {
                fail(vmp, frame, instruction, "K0630",
                     "this machine has handed out %u places in stores, which "
                     "is all it can tell apart",
                     MOST_STAMPS);
                return false;
            }
            store->generations[index] = ++*rt->stamps;
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
            // The slot keeps the stamp it was handed out with, so a
            // reference made before it was given back still names that stamp
            // and the slot is not live: stale stays stale. What the slot gets
            // when it is handed out again is the next stamp there is, which is
            // one nothing else carries.
            store->free_slots[store->free_count++] = index;
            store->count--;
            // A store with nothing in it walks nothing. Everything a walk
            // would step over is dead, and what each slot has counted is kept,
            // so a reference from before is as stale as it was.
            if (store->count == 0) {
                store->used = 0;
                store->free_count = 0;
            }
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
                kest_diags_suggest(vmp->diags,
                                   "it was writing a value as %zu bytes of "
                                   "text",
                                   length);
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
                kest_diags_suggest(vmp->diags,
                                   "it was writing a number as %d bytes of "
                                   "text",
                                   written);
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
            // The same ceiling an array has, and text is where a program
            // reaches it without meaning to: two of these joined is a new one
            // as long as both, so a program doubling one arrives here in
            // thirty steps. `len` counts bytes and gives back an `i32`.
            if (length > (size_t)MAX_COUNTED) {
                fail(vmp, frame, instruction, "K0630",
                     "this text would hold %zu, which is more than `len` can "
                     "count",
                     length);
                return false;
            }
            char *text = kest_arena_alloc(rt->heap, length + 1, 1);
            if (text == NULL) {
                no_room(vmp, frame, instruction, rt);
                kest_diags_suggest(vmp->diags,
                                   "it was joining text into %zu bytes",
                                   length);
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
                kest_diags_suggest(vmp->diags,
                                   "it was making %u bytes of text out of an "
                                   "array",
                                   bytes->length);
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
            // Walked to rather than measured, the way a cut and the rest are:
            // a byte at a place costs the walk to that place. Measuring first
            // made reading the first byte of a line cost the whole line, which
            // is what a loop over text pays on every step. See D372.
            int64_t seen = 0;
            while (seen < index && text[seen] != '\0') {
                seen++;
            }
            if (index < 0 || seen < index || text[index] == '\0') {
                // Measured only to say so: the refusal names how long the
                // text was, and the run that pays for the rest of the walk is
                // the one that is stopping.
                size_t length = seen + strlen(text + seen);
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
#if KEST_CHECKED
            // The one read in this language that does not ask. What makes it
            // right is the walk: the handle was taken and the length measured
            // before the first turn, so the place is there. A build that
            // checks itself asks anyway, because nothing else can — a byte
            // past the end of a piece of text is a byte the arena handed out
            // for something else, so it is neither poisoned nor unmapped and
            // the sanitisers see nothing. Walked rather than measured, the
            // way everything else that reaches a place in text is. See D409.
            int64_t seen = 0;
            while (seen < index && text[seen] != '\0') {
                seen++;
            }
            if (index < 0 || seen < index || text[index] == '\0') {
                fail(vmp, frame, instruction, "K0645",
                     "a walk read byte %lld of text of %zu bytes",
                     (long long)index, (size_t)seen + strlen(text + seen));
                kest_diags_fault(vmp->diags,
                                 "a walk over text measures it before its "
                                 "first turn and reads without asking");
                return false;
            }
#endif
            (top++)->integer = (unsigned char)text[index];
            break;
        }
        case KEST_OP_TEXT_SLICE: {
            int64_t count = (--top)->integer;
            int64_t from = (--top)->integer;
            const char *text = (--top)->text;

            // Walked to rather than measured, the way `rest` is: what a cut
            // costs is the part it reaches and not the part after it, and
            // measuring first is that walk done twice. Cutting three bytes out
            // of a megabyte read the megabyte. See D371.
            int64_t want = from < 0 || count < 0 ? 0 : from + count;
            int64_t seen = 0;
            while (seen < want && text[seen] != '\0') {
                seen++;
            }
            if (from < 0 || count < 0 || seen < want) {
                // Measured only to say so: a refusal names the length, and
                // what it costs to say is paid by the run that is stopping.
                size_t length = seen + strlen(text + seen);
                fail(vmp, frame, instruction, "K0604",
                     "%lld bytes from %lld is outside text of %zu bytes",
                     (long long)count, (long long)from, length);
                return false;
            }
            // A cut that ends where the text already ends is a place inside
            // it: the nought after it is the one that was there, so there is
            // nothing to copy. That is what `rest` is, and this is the same
            // question asked with a length — `slice(t, i, len(t) - i)` is the
            // rest of it however it is spelled. See D370.
            if (text[want] == '\0') {
                (top++)->text = text + from;
                break;
            }
            char *piece = kest_arena_alloc(rt->heap, (size_t)count + 1, 1);
            if (piece == NULL) {
                no_room(vmp, frame, instruction, rt);
                // Measured here for the same reason a refusal measures: the
                // run is stopping either way, and what it is stopping in the
                // middle of is what a reader wants to know.
                kest_diags_suggest(vmp->diags,
                                   "it was taking %lld bytes out of text of "
                                   "%zu",
                                   (long long)count,
                                   (size_t)want + strlen(text + want));
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
            // The same walk, and it may stop where the text does: looking from
            // the end of a text finds nothing, which is an answer rather than
            // a mistake.
            int64_t seen = 0;
            while (seen < from && haystack[seen] != '\0') {
                seen++;
            }
            if (from < 0 || seen < from) {
                fail(vmp, frame, instruction, "K0604",
                     "looking from %lld, which is outside text of %zu bytes",
                     (long long)from, (size_t)seen + strlen(haystack + seen));
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
        case KEST_OP_JUMP_TRUE: {
            uint16_t distance = READ_U16();
            if ((--top)->integer != 0) {
                frame->ip += distance;
            }
            break;
        }
        // The compare and the branch in one. The operands are whole numbers
        // because that is the only pair the compiler fuses.
#define JUMP_UNLESS(expression)                                                \
    do {                                                                       \
        uint16_t distance = READ_U16();                                        \
        KestValue right = *--top;                                              \
        KestValue left = *--top;                                               \
        if (!(expression)) {                                                   \
            frame->ip += distance;                                             \
        }                                                                      \
    } while (0)

#define JUMP_IF(expression)                                                    \
    do {                                                                       \
        uint16_t distance = READ_U16();                                        \
        KestValue right = *--top;                                              \
        KestValue left = *--top;                                               \
        if (expression) {                                                      \
            frame->ip += distance;                                             \
        }                                                                      \
    } while (0)

        case KEST_OP_JUMP_FALSE_LT_I:
            JUMP_UNLESS(left.integer < right.integer);
            break;
        case KEST_OP_JUMP_FALSE_LE_I:
            JUMP_UNLESS(left.integer <= right.integer);
            break;
        case KEST_OP_JUMP_FALSE_GT_I:
            JUMP_UNLESS(left.integer > right.integer);
            break;
        case KEST_OP_JUMP_FALSE_GE_I:
            JUMP_UNLESS(left.integer >= right.integer);
            break;
        case KEST_OP_JUMP_FALSE_EQ_I:
            JUMP_UNLESS(left.integer == right.integer);
            break;
        case KEST_OP_JUMP_FALSE_NE_I:
            JUMP_UNLESS(left.integer != right.integer);
            break;
        case KEST_OP_JUMP_TRUE_LT_I:
            JUMP_IF(left.integer < right.integer);
            break;
        case KEST_OP_JUMP_TRUE_LE_I:
            JUMP_IF(left.integer <= right.integer);
            break;
        case KEST_OP_JUMP_TRUE_GT_I:
            JUMP_IF(left.integer > right.integer);
            break;
        case KEST_OP_JUMP_TRUE_GE_I:
            JUMP_IF(left.integer >= right.integer);
            break;
        case KEST_OP_JUMP_TRUE_EQ_I:
            JUMP_IF(left.integer == right.integer);
            break;
        case KEST_OP_JUMP_TRUE_NE_I:
            JUMP_IF(left.integer != right.integer);
            break;
        case KEST_OP_JUMP_FALSE_LT_F:
            JUMP_UNLESS(left.real < right.real);
            break;
        case KEST_OP_JUMP_FALSE_LE_F:
            JUMP_UNLESS(left.real <= right.real);
            break;
        case KEST_OP_JUMP_FALSE_GT_F:
            JUMP_UNLESS(left.real > right.real);
            break;
        case KEST_OP_JUMP_FALSE_GE_F:
            JUMP_UNLESS(left.real >= right.real);
            break;
        case KEST_OP_JUMP_FALSE_EQ_F:
            JUMP_UNLESS(left.real == right.real);
            break;
        case KEST_OP_JUMP_FALSE_NE_F:
            JUMP_UNLESS(left.real != right.real);
            break;
        case KEST_OP_JUMP_TRUE_LT_F:
            JUMP_IF(left.real < right.real);
            break;
        case KEST_OP_JUMP_TRUE_LE_F:
            JUMP_IF(left.real <= right.real);
            break;
        case KEST_OP_JUMP_TRUE_GT_F:
            JUMP_IF(left.real > right.real);
            break;
        case KEST_OP_JUMP_TRUE_GE_F:
            JUMP_IF(left.real >= right.real);
            break;
        case KEST_OP_JUMP_TRUE_EQ_F:
            JUMP_IF(left.real == right.real);
            break;
        case KEST_OP_JUMP_TRUE_NE_F:
            JUMP_IF(left.real != right.real);
            break;
#undef JUMP_UNLESS
#undef JUMP_IF

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
                what_it_needed(vmp, rt, entry);
                return false;
            }
            KestValue *base = top - argument_slots;
            if (base + callee->slot_count + callee->stack_needed > rt->limit) {
                // With the number, because `out of stack` on its own tells
                // a reader nothing about how much there was: the two ways to
                // answer it are fewer frames and a host that asks for more,
                // and both need the number. See D523.
                fail(vmp, frame, instruction, "K0602",
                     "this call wants more than the %u slots of stack there "
                     "are", rt->stack_slots);
                what_it_needed(vmp, rt, entry);
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

            // A promise is proved twice: over the tree, and over the code
            // that was emitted for it. The second proof follows `call` and
            // stops here, because which chunk this enters is not known until
            // it runs. It is known now, and a chunk carries what it promised,
            // so the one call that proof cannot see through is checked where
            // it is made. See D058.
            if (frame->chunk->no_alloc && !callee->no_alloc) {
                const char *promised = frame->chunk->wrote;
                const char *entered = callee->wrote;
                fail(vmp, frame, instruction, "K0623",
                     "`%s` promises `no.alloc` and this enters `%s`, which "
                     "does not",
                     promised, entered);
                kest_diags_fault(vmp->diags,
                                 "the shape it was held in promises and the "
                                 "body does not");
                return false;
            }

            if (rt->frame_count == rt->call_depth) {
                fail(vmp, frame, instruction, "K0602",
                     "calls nest more than %u deep", rt->call_depth);
                what_it_needed(vmp, rt, entry);
                return false;
            }
            KestValue *base = top - argument_slots;
            if (base + callee->slot_count + callee->stack_needed > rt->limit) {
                // The same sentence in front of the other call instruction,
                // which is the pair D440 is about. See D523.
                fail(vmp, frame, instruction, "K0602",
                     "this call wants more than the %u slots of stack there "
                     "are", rt->stack_slots);
                what_it_needed(vmp, rt, entry);
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
            // What a host was told against what this turned out to be. A
            // host sizes a stack from `kest_needs_from` and then calls back in
            // from here, so a number that is too small is a host that runs out
            // of room somewhere it was told it would not. The floor of this
            // run is where a host function above it left the machine, which is
            // the same place a call back in would start from.
            if (rt->host_measured) {
                const KestValue *floor =
                    rt->running_top != NULL ? rt->running_top : rt->stack;
                uint32_t deep = rt->frame_count - rt->running_frames;
                uint32_t wide = (uint32_t)(top - floor);
                if (deep > rt->host_frames || wide > rt->host_slots) {
                    fail(vmp, frame, instruction, "K0633",
                         "`%s` calls into the host %u slots and %u frames in, "
                         "where `%s` was measured at %u and %u",
                         frame->chunk->name, wide, deep,
                         rt->host_where != NULL ? rt->host_where : "nothing",
                         rt->host_slots, rt->host_frames);
                    kest_diags_fault(vmp->diags,
                                     "what a host is told it needs to call "
                                     "back in from here is that measurement");
                    return false;
                }
            }
            KestValue *was_top = rt->running_top;
            uint32_t was_frames = rt->running_frames;
            rt->running_top = top;
            rt->running_frames = rt->frame_count;
            // The one promise in this language that somebody else keeps. A
            // declaration says a host function does not reach the heap, the
            // compiler lets a `no.alloc` body call it on the strength of that,
            // and nothing but this would notice a host that made text in it.
            bool promised = module->externs[index].promises;
            size_t held = promised ? kest_heap_used(rt) : 0;
            natives[index](base, rt, rt->contexts[index]);
            if (promised && kest_heap_used(rt) != held) {
                rt->running_top = was_top;
                rt->running_frames = was_frames;
                fail(vmp, frame, instruction, "K0631",
                     "`%s` promises `no.alloc` and this host took %zu bytes in "
                     "it",
                     module->externs[index].name, kest_heap_used(rt) - held);
                return false;
            }
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

// The module is taken as something to write to rather than only to read,
// because one thing in it is: what the next place handed out in a store is
// stamped with belongs to the build, so two machines made from it are two
// worlds of one program rather than two programs counting from one.
KestRuntime *kest_runtime_new(KestModule *stamped, const KestHost *host,
                              KestDiags *diags, const KestLimits *limits,
                              const KestWalk *walked) {
    // The machine's own arena, taken before the machine is: everything below
    // that is this machine's rather than the program's comes out of it.
    KestArena *own = kest_arena_new();
    if (own == NULL) {
        kest_diags_starve(diags);
        return NULL;
    }
    KestRuntime *rt = KEST_ARENA_NEW(own, KestRuntime);
    if (rt == NULL) {
        // No room for the machine itself, which is before there is anywhere to
        // write what happened: K0638 below is a host asking for more than
        // there is, and this is the host that asked for nothing and still
        // could not have it.
        kest_arena_free(own);
        kest_diags_starve(diags);
        return NULL;
    }
    rt->own = own;
    const KestModule *module = stamped;
    rt->module = module;
    rt->diags = diags;
    rt->said_before = diags->count;
    rt->reported = diags->count;
    // What a walk of the whole program says, which the build works out once
    // and hands over: what it needs, where it calls into the host and which
    // function that is. A machine used to walk it here, in its own room and
    // handed back after, so a host that makes a machine a frame did the same
    // walk every frame — 1596 bytes of scratch for `examples/embed.kest`
    // against the 600 a machine is made of. The module does not change after
    // it is compiled, so neither does the answer. See D607.
    rt->host_measured = walked->measured;
    rt->host_slots = walked->host_slots;
    rt->host_frames = walked->host_frames;
    rt->host_where = walked->measured ? walked->why.where : NULL;
    uint32_t reached = walked->slots;
    uint32_t deep = walked->frames;

    // And what a host that says nothing gets, which is what the program asked
    // for: the worst any function needs, plus the worst call back in from
    // inside a host function — a machine does not know which function a host
    // will call, and a host that binds one may be called from inside it. A
    // program with no deepest call has no number to give, and then the usual
    // ones are what there is. Half a megabyte of stack for a program that
    // wants sixteen slots is what saying nothing used to cost. See D575.
    uint32_t wants_slots = STACK_SLOTS;
    uint32_t wants_frames = MAX_FRAMES;
    if (rt->host_measured && reached + rt->host_slots > 0) {
        wants_slots = reached + rt->host_slots;
        wants_frames = deep + rt->host_frames;
    }
    rt->stack_slots = limits == NULL || limits->stack_slots == 0
                          ? wants_slots
                          : limits->stack_slots;
    rt->call_depth = limits == NULL || limits->call_depth == 0
                         ? wants_frames
                         : limits->call_depth;
    rt->stack = KEST_ARENA_ARRAY(own, KestValue, rt->stack_slots);
    rt->frames = KEST_ARENA_ARRAY(own, Frame, rt->call_depth);
    rt->natives =
        KEST_ARENA_ARRAY(own, KestNative, module->extern_count + 1);
    rt->contexts = KEST_ARENA_ARRAY(own, void *, module->extern_count + 1);
    rt->said_extern = KEST_ARENA_ARRAY(own, uint8_t, module->extern_count + 1);
    rt->said_copy = KEST_ARENA_ARRAY(own, uint8_t, module->count + 1);
    rt->said_layout = KEST_ARENA_ARRAY(own, uint8_t, module->layout_count + 1);
    rt->heap = kest_arena_new();
    rt->heap_bytes = limits == NULL ? 0 : limits->heap_bytes;
    if (rt->heap != NULL) {
        kest_arena_cap(rt->heap, rt->heap_bytes);
    }
    if (rt->stack == NULL || rt->frames == NULL || rt->natives == NULL ||
        rt->contexts == NULL || rt->said_extern == NULL ||
        rt->said_copy == NULL || rt->said_layout == NULL || rt->heap == NULL) {
        // A host says how much stack and how deep the calls may go, and both
        // are taken before anything runs. Asking for more than the machine
        // this is on can give came back as nothing at all: a host with a
        // number too big for the machine and a host with a program that would
        // not compile got the same nothing, and only one of them is about the
        // program. Which of them could not be had is said, because a host that
        // halves the wrong number is a host halving it forever.
        KestSpan nowhere = {0, 0};
        kest_diags_in(diags, NULL);
        if (rt->stack == NULL) {
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0638", nowhere,
                           "this host asked for %u slots of stack and this "
                           "machine cannot have that much",
                           rt->stack_slots);
        } else if (rt->frames == NULL) {
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0638", nowhere,
                           "this host asked for calls %u deep and this machine "
                           "cannot have that many",
                           rt->call_depth);
        } else {
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0638", nowhere,
                           "this machine has nowhere to put what a program "
                           "needs before it runs");
        }
        kest_diags_suggest(diags,
                           "`kest_needs` says what the program wants; a "
                           "number a host picks over that is a number this "
                           "machine has to be able to take");
        kest_arena_free(rt->heap);
        kest_arena_free(own);
        return NULL;
    }
    rt->limit = rt->stack + rt->stack_slots;
    // The build's rather than this machine's, and a machine may be made from a
    // module nobody is counting for, which is its own count starting at
    // nought.
    rt->stamps = &stamped->stamps;
    // And what says this machine is standing on the build, counted where the
    // machines are rather than where the builds are: freeing the build while
    // one of these is up takes the program out from under it.
    rt->standing = &stamped->machines;

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
        // Both of them, because a machine that never started is a machine
        // nobody can free: what it took is the machine's own since D574, and
        // the last door out is the one that has to put it back.
        kest_arena_free(rt->heap);
        kest_arena_free(own);
        return NULL;
    }
    // Counted here rather than where machines are asked for, so that what
    // counts one up is beside what counts it down and a machine that was never
    // made was never counted.
    ++*rt->standing;
    // And from here what this machine says is written in its own room rather
    // than in the build's. Everything above this line is said by a machine
    // that never started, which is a machine nobody can ask: those words are
    // the build's, because the build is what a host has then. See D574 and
    // D617.
    diags->arena = own;
    // And what it keeps of what nobody asks for. A machine does not end, so
    // this is the one list in this project with a ceiling. See D618.
    diags->most = KEST_MOST_UNREAD;
    rt->after_said = kest_arena_mark(own);
    return rt;
}

// Whether the program is in the middle of running, which it is exactly when a
// function the host bound is on the stack. Anything that would take the heap
// or the machine out from under it is refused there. See D073.
static bool is_running(const KestRuntime *runtime) {
    return runtime != NULL && runtime->running_top != NULL;
}

bool kest_runtime_free(KestRuntime *runtime) {
    if (runtime == NULL) {
        // Nothing to free is not a refusal: what a host asked for is that
        // there be no machine, and there is none.
        return true;
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
        return false;
    }
    // Read before the arena this machine is in goes, because this struct is in
    // it: what is being freed here is the thing holding the pointers to what
    // is being freed.
    KestArena *own = runtime->own;
    kest_arena_free(runtime->heap);
    --*runtime->standing;
    kest_arena_free(own);
    return true;
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

size_t kest_runtime_cost(const KestRuntime *runtime) {
    return runtime == NULL ? 0 : kest_arena_used(runtime->own);
}

size_t kest_heap_wanted(const KestRuntime *runtime) {
    return runtime == NULL ? 0 : kest_arena_refused(runtime->heap);
}

KestRefusal kest_heap_refused_by(const KestRuntime *runtime) {
    // Read beside the number rather than instead of it: what an arena
    // remembers is the last refusal, and until there has been one there is
    // nothing to say about who made it.
    if (runtime == NULL || kest_arena_refused(runtime->heap) == 0) {
        return KEST_REFUSED_NOTHING;
    }
    return kest_arena_refused_by_ceiling(runtime->heap) ? KEST_REFUSED_CEILING
                                                        : KEST_REFUSED_MACHINE;
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
    // The same heap, emptied. It was a new one and a free of the old one,
    // which is a call to the host and back every time round a loop that
    // resets, and a host that resets is a host with a frame to fit into.
    kest_arena_reset(runtime->heap);
    // Every one of those was on it, and so was the list of what is lent.
    runtime->spare_lends = NULL;
    runtime->lent = NULL;
    runtime->lent_count = 0;
    runtime->lent_capacity = 0;
    return true;
}

void kest_report(KestRuntime *runtime, FILE *out, KestForm form) {
    if (runtime == NULL || out == NULL) {
        return;
    }
    uint32_t from = runtime->reported > runtime->said_before
                        ? runtime->reported
                        : runtime->said_before;
    bool starving = runtime->diags->starved && !runtime->starve_said;
    if (from >= runtime->diags->count && !starving) {
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
    tail.starved = starving;
    tail.error_count += starving ? 1 : 0;
    if (form == KEST_FORM_JSON) {
        kest_diags_render_json(&tail, out);
    } else {
        kest_diags_render(&tail, out);
    }
    runtime->starve_said = runtime->starve_said || starving;
    // And back where it stood. What a host has been told is the host's — it
    // is written wherever the host asked for it — and the room the words were
    // in is this machine's. A program refused every frame says the same
    // sentence every frame, and a machine that kept every one of them would
    // hold a frame's words for as long as it ran: 625 bytes a call, measured
    // on a program whose heap is full. What is handed back is exactly what
    // was said, because nothing else is written here between two readings.
    // See D617.
    runtime->diags->items = NULL;
    runtime->diags->count = 0;
    runtime->diags->capacity = 0;
    runtime->diags->error_count = 0;
    runtime->diags->not_said = 0;
    runtime->diags->held_back = false;
    runtime->reported = 0;
    runtime->said_before = 0;
    kest_arena_rewind(runtime->own, runtime->after_said);
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
        if (runtime->said_extern[i] != 0) {
            return true;
        }
        runtime->said_extern[i] = 1;
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
    // The same again for a generic, under the first of the copies: the list
    // is what the name stands for, and the name stands for the same copies
    // however often it is asked for.
    if (runtime->said_copy[copies[0]] != 0) {
        return true;
    }
    runtime->said_copy[copies[0]] = 1;
    // The names themselves, all four of them, in the arena. This was a
    // hundred and ninety-two bytes and stopped where they ran out, so a host
    // asking about a generic — whose copies are compiled under names with
    // their types written into them — was given a list that ended mid-name
    // and said nothing about it.
    uint32_t shown = count < 4 ? count : 4;
    size_t room = 1;
    for (uint32_t i = 0; i < shown; i++) {
        room += strlen(module->functions[copies[i]]->name) + 5;
    }
    char *list = kest_arena_alloc(runtime->diags->arena, room, 1);
    if (list == NULL) {
        return false;
    }
    size_t at = 0;
    for (uint32_t i = 0; i < shown; i++) {
        at += (size_t)snprintf(list + at, room - at, "%s`%s`",
                               at == 0 ? "" : ", ",
                               module->functions[copies[i]]->name);
    }
    // Two functions may share a name when they take different things, and a
    // generic is compiled once for each set of types it is used with. Both
    // are several functions under one name, and what a host does about it is
    // the same, so this does not guess which it was.
    //
    // Where, though, it can say now: a chunk carries the declaration it was
    // compiled from, so the refusal points at one of them and names the rest
    // of the places. Copies of a generic are all written in the one place, so
    // that is one place said once; two functions of a name are two, and the
    // difference is a thing a host writer can see rather than work out. This
    // said nothing about where anything was. See D613.
    const KestChunk *first = module->functions[copies[0]];
    kest_diags_in(runtime->diags, first->source);
    kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0615",
                   first->declared,
                   "`%s` is more than one function here: they take different "
                   "things",
                   name);
    for (uint32_t i = 1; i < shown; i++) {
        const KestChunk *other = module->functions[copies[i]];
        if (other->source == first->source &&
            other->declared.offset == first->declared.offset) {
            continue;
        }
        kest_diags_note(runtime->diags, other->source, other->declared,
                        "and one of them is written here");
    }
    kest_diags_suggest(runtime->diags, "ask for one of them: %s%s", list,
                       count > 4 ? ", and more" : "");
    return true;
}

// The `at`th function of a name in this module, or -1 past the last. An exact
// name is one function and there is no other; anything else is the copies, in
// the order they were compiled.
static int32_t nth_named(const KestModule *module, const char *name,
                         uint32_t at) {
    for (uint32_t i = 0; i < module->count; i++) {
        if (strcmp(module->functions[i]->name, name) == 0) {
            return at == 0 ? (int32_t)i : -1;
        }
    }
    // Counted rather than gathered. This filled sixty-four indexes and
    // answered -1 for the sixty-fifth, so a host walking the copies of a
    // generic stopped there and was told nothing: -1 is how the walk ends,
    // and a walk that ends early ends the same way one that ends says it
    // does. Nothing has to be held to find the one at a place.
    size_t length = strlen(name);
    uint32_t seen = 0;
    for (uint32_t i = 0; i < module->count; i++) {
        const char *candidate = module->functions[i]->name;
        if (strncmp(candidate, name, length) != 0 || candidate[length] != '#') {
            continue;
        }
        if (seen == at) {
            return (int32_t)i;
        }
        seen++;
    }
    return -1;
}

int32_t kest_entry_of(KestRuntime *runtime, const char *name, uint32_t at) {
    int32_t found = nth_named(runtime->module, name, at);
    if (found >= 0) {
        return found;
    }
    const char *alias = runtime->module->alias;
    size_t prefix = strlen(alias);
    char qualified[256];
    if (prefix == 0 || prefix + strlen(name) + 2 > sizeof(qualified)) {
        return -1;
    }
    memcpy(qualified, alias, prefix);
    qualified[prefix] = '.';
    memcpy(qualified + prefix + 1, name, strlen(name) + 1);
    return nth_named(runtime->module, qualified, at);
}

int32_t kest_entry(KestRuntime *runtime, const char *name) {
    // A host writes what the file writes, and the file registered its names
    // under itself; which of the two spellings it is is `kest_module_entry`'s
    // to know, and every part of this project asks it the same way.
    int32_t found = kest_module_entry(runtime->module, name);
    if (found >= 0) {
        return found;
    }
    const char *alias = runtime->module->alias;
    size_t prefix = alias == NULL ? 0 : strlen(alias);
    char qualified[256];
    bool composed = prefix != 0 && prefix + strlen(name) + 2 <= sizeof(qualified);
    if (composed) {
        memcpy(qualified, alias, prefix);
        qualified[prefix] = '.';
        memcpy(qualified + prefix + 1, name, strlen(name) + 1);
    }
    if (!explain_entry(runtime, name) && composed) {
        explain_entry(runtime, qualified);
    }
    return -1;
}

// Declared here because the walk that says there is nothing at an index is the
// same walk for every one of these, and the one that has it is written below.
static const KestChunk *frame_of(KestRuntime *runtime, int32_t entry,
                                 const uint8_t *kinds, uint32_t count);

// Every question about a frame answers a host with a number or a pointer, and
// for each of them the answer that means nothing here is one a real frame can
// give: nought arguments, nought slots in, nothing past the last one, nothing
// given back. So an index that is no function reads as a function that takes
// and gives nothing, which is what `kest_frame_slots` says out loud and what
// these said in silence. `frame_of` is the one place that says it. See D436.
const char *kest_entry_name(KestRuntime *runtime, int32_t entry) {
    // Its own bounds rather than `frame_of`'s: that one says what went wrong,
    // and a walk of everything a program defines ends at the end. A machine
    // that complained there is what every host walking the list would be told
    // for reading it to the end. See D584 and D609.
    if (runtime == NULL || entry < 0 ||
        (uint32_t)entry >= runtime->module->count) {
        return NULL;
    }
    return runtime->module->functions[entry]->name;
}

const char *kest_entry_wrote(KestRuntime *runtime, int32_t entry) {
    if (runtime == NULL || entry < 0 ||
        (uint32_t)entry >= runtime->module->count) {
        return NULL;
    }
    return runtime->module->functions[entry]->wrote;
}

uint32_t kest_frame_takes(KestRuntime *runtime, int32_t entry) {
    const KestChunk *chunk = frame_of(runtime, entry, NULL, 0);
    if (chunk == NULL) {
        return 0;
    }
    return chunk->takes_count;
}

uint32_t kest_frame_at(KestRuntime *runtime, int32_t entry, uint32_t which) {
    if (frame_of(runtime, entry, NULL, 0) == NULL) {
        return 0;
    }
    const KestChunk *chunk = runtime->module->functions[entry];
    uint32_t at = 0;
    for (uint32_t i = 0; i < which && i < chunk->takes_count; i++) {
        // One piece a slot, so what a layout holds is how wide the argument
        // is as well as what is in it.
        at += runtime->module->layouts[chunk->takes[i]].count;
    }
    return at;
}

const KestLayout *kest_frame_gives(KestRuntime *runtime, int32_t entry) {
    const KestChunk *chunk = frame_of(runtime, entry, NULL, 0);
    if (chunk == NULL) {
        return NULL;
    }
    // Nothing is what a function that gives nothing gives, and a layout for
    // it would be a shape for something that is not there.
    return chunk->returns_value ? &runtime->module->layouts[chunk->gives]
                                : NULL;
}

// Whether writing this value would read a piece of text that is not there.
// The same walk `format_value` makes, asked first: a frame nothing has been
// called with is noughts, and a nought where text goes is not an empty piece
// of text but the absence of one.
static bool missing_text(const KestType *type, const KestValue *slots) {
    switch (type->tag) {
    case KEST_T_TEXT:
        return slots[0].text == NULL;
    case KEST_T_ENUM: {
        uint32_t which = (uint32_t)slots[0].integer;
        if (which >= type->case_count) {
            return false;
        }
        const KestVariantType *variant = &type->cases[which];
        for (uint32_t p = 0; p < variant->payload_count; p++) {
            if (missing_text(variant->payload[p], slots + variant->offsets[p])) {
                return true;
            }
        }
        return false;
    }
    case KEST_T_OPTIONAL:
        if (slots[type->element->slots].integer == 0) {
            return false;
        }
        return missing_text(type->element, slots);
    // Everything else either holds no text or is a shape this never writes,
    // and both are written out rather than left to a `default` for the reason
    // `format_value` gives beside the same list.
    case KEST_T_BOOL:
    case KEST_T_INT:
    case KEST_T_FLOAT:
    case KEST_T_FLAGS:
    case KEST_T_ERROR:
    case KEST_T_VOID:
    case KEST_T_STRUCT:
    case KEST_T_ARRAY:
    case KEST_T_FIXED:
    case KEST_T_REF:
    case KEST_T_STORE:
    case KEST_T_FN:
    case KEST_T_PARAM:
    case KEST_T_MODULE:
        return false;
    }
    return false;
}

int64_t kest_gave_text(KestRuntime *runtime, int32_t entry,
                       const KestValue *frame, char *out, size_t room) {
    const KestChunk *chunk = frame_of(runtime, entry, NULL, 0);
    if (chunk == NULL) {
        return -1;
    }
    KestSpan nothing = {0, 0};
    const char *called = chunk->wrote;
    // Minus one used to be all three of these, said in silence: a host got a
    // number that means no and a report that said nothing, and could not tell
    // an index that is no function from a function with nothing to say.
    if (!chunk->returns_value) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0646", nothing,
                       "`%s` gives nothing back, so there is nothing to write",
                       called);
        kest_diags_suggest(runtime->diags,
                           "`kest_frame_gives` is NULL for a function that "
                           "gives nothing back, which is what to ask first");
        return -1;
    }
    const KestType *type = runtime->module->layout_types[chunk->gives];
    // `kest_type_has_text` says which type it was that has none, and this is
    // the somewhere it wanted to be said.
    const KestType *without = NULL;
    if (type == NULL || !kest_type_has_text(type, &without)) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0646", nothing,
                       "`%s` gives back `%s`, which has no text of its own",
                       called,
                       kest_type_name(runtime->diags->arena,
                                      without != NULL ? without : type));
        kest_diags_suggest(runtime->diags,
                           "walk it with `kest_frame_gives` and write what is "
                           "there: what a struct, a run, a store or a "
                           "reference means is the host's to decide");
        return -1;
    }

    // Nothing in the slot is a frame that has not been called with, which is
    // a host asking what came back before anything came back. Reading it as
    // text would be reading whatever the frame was made with, and a host that
    // made one out of nothing has a nought there.
    //
    // A frame that is not there at all is the same news, and it is what a host
    // that could not make one hands over. `kest_call` and `kest_takes_text`
    // both refuse it; this one read slot zero. See D511.
    if (frame == NULL || missing_text(type, frame)) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(runtime->diags, NULL);
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0632", nowhere,
                       "nothing is in the frame to say, so nothing was called "
                       "with it");
        kest_diags_suggest(runtime->diags,
                           "call it with `kest_call` first; this says what is "
                           "there rather than putting something there");
        return -1;
    }

    // Text on its own is what it holds rather than the source that spells it,
    // which is the exception D035 names: a hole holding one writes the
    // content, and this is the same question asked from outside.
    size_t needed = type->tag == KEST_T_TEXT
                        ? strlen(frame[0].text)
                        : kest_write_value(NULL, 0, type, frame);
    if (out != NULL && room > 0) {
        size_t fits = needed < room - 1 ? needed : room - 1;
        if (type->tag == KEST_T_TEXT) {
            memcpy(out, frame[0].text, fits);
        } else {
            kest_write_value(out, fits, type, frame);
        }
        out[fits] = '\0';
    }
    return (int64_t)needed;
}

const KestLayout *kest_frame_layout(KestRuntime *runtime, int32_t entry,
                                    uint32_t which) {
    const KestChunk *chunk = frame_of(runtime, entry, NULL, 0);
    if (chunk == NULL) {
        return NULL;
    }
    // Past the last one is the walk ending, which is not the same news and is
    // said by nobody: a host walks the arguments until this answers nothing.
    if (which >= chunk->takes_count) {
        return NULL;
    }
    return &runtime->module->layouts[chunk->takes[which]];
}

// What a host says about a run of slots, against what the program says they
// are. Filling a frame and reading one back are the same disagreement in the
// two directions, so they are the same walk: the layouts are the arguments in
// one and what comes back in the other, and the words are what differ.
static bool frame_agrees(KestRuntime *runtime, const KestChunk *chunk,
                         const uint16_t *which, uint32_t layouts,
                         const uint8_t *kinds, uint32_t count,
                         const char *said, const char *ask) {
    KestSpan nowhere = {0, 0};
    const char *name = chunk->wrote;

    // How many slots there are before what is in them: a host that said too
    // few has not checked the rest, and telling it about the first slot it did
    // say would send it looking at the wrong end of its own frame.
    uint32_t slots = 0;
    for (uint32_t i = 0; i < layouts; i++) {
        slots += runtime->module->layouts[which[i]].count;
    }
    if (count != slots) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0634", nowhere,
                       "`%s` %s %u slot%s and this host says what %u of them "
                       "hold",
                       name, said, slots, slots == 1 ? "" : "s", count);
        kest_diags_suggest(runtime->diags,
                           "`kest_frame_slots` says how wide it is, and every "
                           "one of them is a slot something is in");
        return false;
    }

    uint32_t at = 0;
    for (uint32_t i = 0; i < layouts; i++) {
        const KestLayout *layout = &runtime->module->layouts[which[i]];
        for (uint16_t p = 0; p < layout->count; p++) {
            if (kinds[at] != layout->pieces[p].kind) {
                kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0634",
                               nowhere,
                               "`%s` %s `%s` in slot %u and this host says "
                               "`%s`",
                               name, said,
                               kest_scalar_name(layout->pieces[p].kind), at,
                               kest_scalar_name(kinds[at]));
                // The words are the caller's and the numbers are none, which
                // is the one shape a message can have that says nothing about
                // what to put where.
                kest_diags_suggest(runtime->diags, "%s", ask);
                return false;
            }
            at++;
        }
    }
    return true;
}

// Whether a host may be asked about this at all, and which function it is.
static const KestChunk *frame_of(KestRuntime *runtime, int32_t entry,
                                 const uint8_t *kinds, uint32_t count) {
    KestSpan nowhere = {0, 0};
    kest_diags_in(runtime->diags, NULL);
    // A host that says how many slots it is about to describe and hands
    // nothing to read them from. This was the sentence below, which is about
    // an index and says nothing about what is wrong here: the index may be a
    // function and often is. It is said every time, because it is about this
    // call rather than about the program. See D615.
    if (kinds == NULL && count > 0) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0634", nowhere,
                       "this host says what %u slot%s hold and handed nothing "
                       "to read them from",
                       count, count == 1 ? "" : "s");
        kest_diags_suggest(runtime->diags,
                           "what a frame holds is a kind a slot, and no slots "
                           "is nought of them");
        return NULL;
    }
    if (entry < 0 || (uint32_t)entry >= runtime->module->count) {
        // How many there are, the way the walk of what a program asks the
        // host for says it (`K0648`): two walks past the end, and one of them
        // said how far the list went and the other left a host to find out.
        // A host walking what a program defines reads the same number. See
        // D614.
        //
        // Once, like the other statements about a program a host can ask for
        // twice: how many functions there are does not change while a machine
        // runs, and saying it again cost 648 bytes an asking to a host that
        // asked this instead of `kest_entry_name`, which answers the same
        // question for nothing and says nothing. See D608 and D615.
        if (!runtime->said_no_frame) {
            runtime->said_no_frame = true;
            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0634",
                           nowhere,
                           "this program defines %u function%s and there is "
                           "nothing at %d to say what a frame holds",
                           runtime->module->count,
                           runtime->module->count == 1 ? "" : "s", entry);
            kest_diags_suggest(runtime->diags,
                               "`kest_entry_name` is NULL for an index that is "
                               "no function, and says nothing about it");
        }
        return NULL;
    }
    return runtime->module->functions[entry];
}

bool kest_frame_fills(KestRuntime *runtime, int32_t entry,
                      const uint8_t *kinds, uint32_t count) {
    const KestChunk *chunk = frame_of(runtime, entry, kinds, count);
    if (chunk == NULL) {
        return false;
    }
    return frame_agrees(runtime, chunk, chunk->takes, chunk->takes_count,
                        kinds, count, "takes",
                        "`kest_frame_layout` says what each argument is made "
                        "of, a piece a slot");
}

bool kest_frame_reads(KestRuntime *runtime, int32_t entry,
                      const uint8_t *kinds, uint32_t count) {
    const KestChunk *chunk = frame_of(runtime, entry, kinds, count);
    if (chunk == NULL) {
        return false;
    }
    // A function that gives nothing back has nothing to read, and a host that
    // says a slot is read out of it is wrong about that rather than about a
    // kind.
    uint16_t gives = chunk->gives;
    return frame_agrees(runtime, chunk, &gives, chunk->returns_value ? 1 : 0,
                        kinds, count, "gives back",
                        "`kest_frame_gives` says what comes back over the "
                        "frame, a piece a slot");
}

bool kest_takes_text(KestRuntime *runtime, int32_t entry, KestValue *frame,
                     uint32_t slots, const char *const *words,
                     uint32_t count) {
    KestSpan nowhere = {0, 0};
    const KestChunk *chunk = frame_of(runtime, entry, NULL, 0);
    if (chunk == NULL) {
        return false;
    }
    const char *name = chunk->wrote;
    if (count != chunk->takes_count || (words == NULL && count > 0)) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0635", nowhere,
                       "`%s` takes %u argument%s and this host handed over %u",
                       name, chunk->takes_count,
                       chunk->takes_count == 1 ? "" : "s", count);
        kest_diags_suggest(runtime->diags,
                           "one word an argument, and not one a slot: a "
                           "`Vec3` is three slots and no word at all");
        return false;
    }
    // The same width `kest_call` wants, refused before anything is written
    // rather than after: a frame too narrow would be written past here and
    // read past there.
    if (slots < chunk->param_slots || (frame == NULL && slots > 0)) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0635", nowhere,
                       "`%s` takes %u slot%s and this frame holds %u", name,
                       chunk->param_slots, chunk->param_slots == 1 ? "" : "s",
                       slots);
        kest_diags_suggest(runtime->diags,
                           "`kest_frame_slots` says how wide it has to be");
        return false;
    }

    uint32_t at = 0;
    for (uint32_t i = 0; i < count; i++) {
        const KestLayout *layout =
            &runtime->module->layouts[chunk->takes[i]];
        const KestType *type = layout->type;
        const char *why = NULL;
        // Text is the one of them a host cannot hand over by pointing at its
        // own bytes: what a program holds it must own, so it is copied the
        // way anything else a host hands over is copied.
        if (type != NULL && type->tag == KEST_T_TEXT) {
            KestValue given = kest_text(runtime, words[i],
                                        (uint32_t)strlen(words[i]));
            if (given.text == NULL) {
                return false;
            }
            frame[at] = given;
        } else if (!kest_value_read(runtime->diags->arena, words[i], type,
                                    &frame[at], &why)) {
            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0635",
                           nowhere, "`%s` %s, and `%s` takes it", words[i],
                           why, name);
            kest_diags_suggest(runtime->diags,
                               "a word is read as the type the declaration "
                               "says, the way the language writes one");
            return false;
        }
        at += layout->count;
    }
    return true;
}

KestKept kest_kept_where(const KestRuntime *runtime, KestValue kept) {
    if (runtime == NULL || kept.object == NULL) {
        return KEST_KEPT_NOWHERE;
    }
    // The two places a value the host was handed can live, which are the two
    // a call in asks about: what a program made while running, and what the
    // file it came from wrote. Text and handles are the same pointer here —
    // what is being asked about is the memory and not what is written in it.
    if (kest_arena_holds(runtime->heap, kept.object)) {
        // A lend is a header of the machine's in front of a block that is not,
        // and a host asking about one is asking about the block. Found by
        // address in the list of what is lent rather than by reading what is
        // at the address: what is at an address the host handed over is
        // whatever the host handed over, and a piece of text near the end of
        // a block is not four bytes long because a header is.
        for (uint32_t at = 0; at < runtime->lent_count; at++) {
            if ((const void *)runtime->lent[at] == kept.object) {
                return KEST_KEPT_LENT;
            }
        }
        return KEST_KEPT_HEAP;
    }
    if (kest_arena_holds(runtime->module->arena, kept.object)) {
        return KEST_KEPT_PROGRAM;
    }
    return KEST_KEPT_NOWHERE;
}

bool kest_still_holds(const KestRuntime *runtime, KestValue kept) {
    // Read out of the answer above rather than asked again, because two
    // readings of one thing are two things to keep in step.
    return kest_kept_where(runtime, kept) != KEST_KEPT_NOWHERE;
}

bool kest_lend_ends(KestRuntime *runtime, KestValue lent) {
    KestSpan nowhere = {0, 0};
    kest_diags_in(runtime->diags, NULL);
    // The same question a call in asks, for the same reason: what is at an
    // address the machine never handed out is whatever is there.
    if (!kest_arena_holds(runtime->heap, lent.object) ||
        !KEST_HANDLE_IS(lent.object, KEST_IS_ARRAY)) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0637", nowhere,
                       "this is not a lend this machine gave out");
        kest_diags_suggest(runtime->diags,
                           "`kest_borrow` answers what to hand back here, and "
                           "a machine takes back only what it lent");
        return false;
    }
    Array *array = lent.object;
    if (!array->borrowed) {
        kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0637", nowhere,
                       "this array is the program's own and not a lend");
        kest_diags_suggest(runtime->diags,
                           "what the program made is the program's for as "
                           "long as it holds it; only a host's own block is "
                           "taken back");
        return false;
    }
    // The header stays where it is and says what happened to it. Freeing it
    // would put the program back to reading whatever the heap hands out next,
    // which is the whole thing this is for.
    // Every handle over that block and not only the one handed over. A host
    // lending the same memory twice has two handles and one block, and what it
    // takes back is the block: a handle left alive over memory the host has
    // moved on from is the thing ending a lend exists to prevent.
    // What was lent is a run of bytes rather than an address: a host lending
    // the same block twice has two handles over one run, and one lending the
    // tail of a block has two runs that share their ends. Either way the
    // memory is the host's and it is taking it back, so what goes is every
    // handle over any of it.
    const unsigned char *block = array->bytes;
    const unsigned char *block_end =
        block + (size_t)array->length * array->stride;
    uint32_t at = 0;
    while (at < runtime->lent_count) {
        Array *one = runtime->lent[at];
        const unsigned char *from = one->bytes;
        const unsigned char *to = from + (size_t)one->length * one->stride;
        if (one != array && (to <= block || from >= block_end)) {
            at++;
            continue;
        }
        one->what = KEST_WAS_LENT;
        one->length = 0;
        one->capacity = 0;
        one->bytes = (unsigned char *)(void *)runtime->spare_lends;
        runtime->spare_lends = one;
        runtime->lent[at] = runtime->lent[--runtime->lent_count];
    }
    return true;
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
    const KestChunk *chunk = runtime->module->functions[index];
    const char *name = chunk->wrote;
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

    // And what the host is handing over, where what it hands over is a handle.
    // A handle is a pointer and the machine reads four bytes at the front of
    // it to know what it is; any four bytes can be those four. What cannot be
    // faked is having come out of this machine's heap, which is what a walk of
    // the blocks answers — once per call rather than once per instruction, at
    // the crossing where a pointer from outside can arrive at all.
    uint32_t at = 0;
    for (uint32_t which = 0; which < chunk->takes_count; which++) {
        const KestLayout *layout = &runtime->module->layouts[chunk->takes[which]];
        const KestType *type = layout->type;
        // Text is the same question with two places to look: what a program
        // holds is either on the heap, where anything made while running goes,
        // or in the arena the program was compiled into, where the text a file
        // wrote lives. A host's own string is in neither, and a host handing
        // one over is undertaking to keep it as long as the program holds it,
        // which is what `kest_text` exists so that nobody has to do.
        // And nothing at all in a slot that takes text, which is what a host
        // that zeroed a frame and called anyway hands over. Text in this
        // language is never nothing — an empty piece of it is a piece of it —
        // so a slot holding no address is a host that has not filled the
        // frame, and the program reads it at the first thing it does with it.
        // See D629.
        if (type != NULL && type->tag == KEST_T_TEXT &&
            frame[at].text == NULL) {
            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636",
                           nowhere,
                           "`%s` takes text in slot %u and this host handed "
                           "no address",
                           name, at);
            kest_diags_suggest(runtime->diags,
                               "`kest_text` makes text the machine keeps, and "
                               "an empty piece of it is text as well");
            return false;
        }
        if (type != NULL && type->tag == KEST_T_TEXT &&
            frame[at].text != NULL &&
            !kest_arena_holds(runtime->heap, frame[at].text) &&
            !kest_arena_holds(runtime->module->arena, frame[at].text)) {
            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636",
                           nowhere,
                           "`%s` takes text in slot %u and this did not come "
                           "from this machine",
                           name, at);
            kest_diags_suggest(runtime->diags,
                               "`kest_text` copies a host's bytes onto the "
                               "heap, and what it answers is what to hand "
                               "over");
            return false;
        }
        // And a handle slot nobody filled, which is the same mistake as the
        // one above and was caught in a different place: the machine reads the
        // four bytes at the front of a handle at the instruction that uses it
        // and says `K0612` there, which points at the program for something
        // the host did. Said at the door, it names the slot. See D630.
        if (type != NULL &&
            (type->tag == KEST_T_ARRAY || type->tag == KEST_T_STORE) &&
            frame[at].object == NULL) {
            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636",
                           nowhere,
                           "`%s` takes a handle in slot %u and this host "
                           "handed no handle",
                           name, at);
            kest_diags_suggest(runtime->diags,
                               "a handle is what `kest_call` or `kest_borrow` "
                               "gave back, and a frame of noughts is a frame "
                               "nobody filled");
            return false;
        }
        if (type != NULL &&
            (type->tag == KEST_T_ARRAY || type->tag == KEST_T_STORE) &&
            frame[at].object != NULL &&
            !kest_arena_holds(runtime->heap, frame[at].object)) {
            kest_diags_add(runtime->diags, KEST_SEVERITY_ERROR, "K0636",
                           nowhere,
                           "`%s` takes a handle in slot %u and this one did "
                           "not come from this machine",
                           name, at);
            kest_diags_suggest(runtime->diags,
                               "a handle is what `kest_call` or `kest_borrow` "
                               "gave back, and it belongs to the machine that "
                               "gave it");
            return false;
        }
        at += layout->count;
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
