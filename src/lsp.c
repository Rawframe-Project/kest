#include "lsp.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "diag.h"
#include "mem.h"
#include "fmt.h"
#include "types.h"

// A JSON value, which is what a client sends and what has to be read without
// a library. The tree is in an arena that is thrown away with the message, so
// nothing here frees anything.
typedef enum {
    JSON_NOTHING,
    JSON_FALSE,
    JSON_TRUE,
    JSON_NUMBER,
    JSON_TEXT,
    JSON_LIST,
    JSON_OBJECT,
} JsonKind;

typedef struct Json Json;

struct Json {
    JsonKind kind;
    double number;
    // Text, already unescaped, and how long it is: a message may carry a
    // nought inside a string and text that ended at one would be a document
    // cut in half.
    const char *text;
    size_t length;
    // Members for an object, elements for a list. An object keeps its names
    // beside its values in the same order they arrived.
    const char **names;
    size_t *name_lengths;
    Json **items;
    uint32_t count;
    uint32_t capacity;
};

typedef struct {
    const char *at;
    const char *end;
    KestArena *arena;
    bool broke;
} Reading;

static Json *read_value(Reading *reading);

static void skip_space(Reading *reading) {
    while (reading->at < reading->end &&
           (*reading->at == ' ' || *reading->at == '\t' ||
            *reading->at == '\n' || *reading->at == '\r')) {
        reading->at++;
    }
}

static Json *made(Reading *reading, JsonKind kind) {
    Json *one = KEST_ARENA_ARRAY(reading->arena, Json, 1);
    if (one == NULL) {
        reading->broke = true;
        return NULL;
    }
    memset(one, 0, sizeof(*one));
    one->kind = kind;
    return one;
}

static bool room_for_one(Reading *reading, Json *held) {
    if (held->count < held->capacity) {
        return true;
    }
    uint32_t grown = held->capacity == 0 ? 8 : held->capacity * 2;
    Json **items = KEST_ARENA_ARRAY(reading->arena, Json *, grown);
    const char **names = KEST_ARENA_ARRAY(reading->arena, const char *, grown);
    size_t *lengths = KEST_ARENA_ARRAY(reading->arena, size_t, grown);
    if (items == NULL || names == NULL || lengths == NULL) {
        reading->broke = true;
        return false;
    }
    for (uint32_t i = 0; i < held->count; i++) {
        items[i] = held->items[i];
        names[i] = held->names == NULL ? NULL : held->names[i];
        lengths[i] = held->name_lengths == NULL ? 0 : held->name_lengths[i];
    }
    held->items = items;
    held->names = names;
    held->name_lengths = lengths;
    held->capacity = grown;
    return true;
}

// A codepoint written out as UTF-8, which is what `\u` in a message means and
// what the rest of this compiler reads.
static size_t put_utf8(char *out, uint32_t code) {
    if (code < 0x80) {
        out[0] = (char)code;
        return 1;
    }
    if (code < 0x800) {
        out[0] = (char)(0xc0 | (code >> 6));
        out[1] = (char)(0x80 | (code & 0x3f));
        return 2;
    }
    if (code < 0x10000) {
        out[0] = (char)(0xe0 | (code >> 12));
        out[1] = (char)(0x80 | ((code >> 6) & 0x3f));
        out[2] = (char)(0x80 | (code & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (code >> 18));
    out[1] = (char)(0x80 | ((code >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((code >> 6) & 0x3f));
    out[3] = (char)(0x80 | (code & 0x3f));
    return 4;
}

static uint32_t hex_of(const char *at) {
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        char c = at[i];
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value |= (uint32_t)(c - 'A' + 10);
        }
    }
    return value;
}

static Json *read_text(Reading *reading) {
    reading->at++;
    const char *from = reading->at;
    size_t most = (size_t)(reading->end - from) + 1;
    char *out = kest_arena_alloc(reading->arena, most, 1);
    if (out == NULL) {
        reading->broke = true;
        return NULL;
    }
    size_t used = 0;
    while (reading->at < reading->end && *reading->at != '"') {
        if (*reading->at != '\\') {
            out[used++] = *reading->at++;
            continue;
        }
        reading->at++;
        if (reading->at >= reading->end) {
            break;
        }
        char what = *reading->at++;
        switch (what) {
        case 'n': out[used++] = '\n'; break;
        case 't': out[used++] = '\t'; break;
        case 'r': out[used++] = '\r'; break;
        case 'b': out[used++] = '\b'; break;
        case 'f': out[used++] = '\f'; break;
        case 'u': {
            if (reading->end - reading->at < 4) {
                reading->broke = true;
                return NULL;
            }
            uint32_t code = hex_of(reading->at);
            reading->at += 4;
            // A pair of halves is one character. A client writes anything past
            // the first sixty-five thousand as two, so reading the first on
            // its own would put half a character in a file.
            if (code >= 0xd800 && code <= 0xdbff &&
                reading->end - reading->at >= 6 && reading->at[0] == '\\' &&
                reading->at[1] == 'u') {
                uint32_t low = hex_of(reading->at + 2);
                if (low >= 0xdc00 && low <= 0xdfff) {
                    code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                    reading->at += 6;
                }
            }
            used += put_utf8(out + used, code);
            break;
        }
        default: out[used++] = what; break;
        }
    }
    if (reading->at < reading->end) {
        reading->at++;
    }
    out[used] = '\0';
    Json *one = made(reading, JSON_TEXT);
    if (one == NULL) {
        return NULL;
    }
    one->text = out;
    one->length = used;
    return one;
}

static Json *read_value(Reading *reading) {
    skip_space(reading);
    if (reading->at >= reading->end || reading->broke) {
        reading->broke = true;
        return NULL;
    }
    char c = *reading->at;
    if (c == '"') {
        return read_text(reading);
    }
    if (c == '{' || c == '[') {
        bool object = c == '{';
        char closing = object ? '}' : ']';
        reading->at++;
        Json *held = made(reading, object ? JSON_OBJECT : JSON_LIST);
        if (held == NULL) {
            return NULL;
        }
        skip_space(reading);
        if (reading->at < reading->end && *reading->at == closing) {
            reading->at++;
            return held;
        }
        while (reading->at < reading->end && !reading->broke) {
            const char *name = NULL;
            size_t name_length = 0;
            if (object) {
                skip_space(reading);
                Json *key = read_value(reading);
                if (key == NULL || key->kind != JSON_TEXT) {
                    reading->broke = true;
                    return NULL;
                }
                name = key->text;
                name_length = key->length;
                skip_space(reading);
                if (reading->at >= reading->end || *reading->at != ':') {
                    reading->broke = true;
                    return NULL;
                }
                reading->at++;
            }
            Json *value = read_value(reading);
            if (value == NULL || !room_for_one(reading, held)) {
                reading->broke = true;
                return NULL;
            }
            held->names[held->count] = name;
            held->name_lengths[held->count] = name_length;
            held->items[held->count] = value;
            held->count++;
            skip_space(reading);
            if (reading->at < reading->end && *reading->at == ',') {
                reading->at++;
                continue;
            }
            if (reading->at < reading->end && *reading->at == closing) {
                reading->at++;
                return held;
            }
            reading->broke = true;
            return NULL;
        }
        reading->broke = true;
        return NULL;
    }
    if (c == 't' && reading->end - reading->at >= 4) {
        reading->at += 4;
        return made(reading, JSON_TRUE);
    }
    if (c == 'f' && reading->end - reading->at >= 5) {
        reading->at += 5;
        return made(reading, JSON_FALSE);
    }
    if (c == 'n' && reading->end - reading->at >= 4) {
        reading->at += 4;
        return made(reading, JSON_NOTHING);
    }
    char *after = NULL;
    double value = strtod(reading->at, &after);
    if (after == reading->at) {
        reading->broke = true;
        return NULL;
    }
    reading->at = after;
    Json *one = made(reading, JSON_NUMBER);
    if (one == NULL) {
        return NULL;
    }
    one->number = value;
    return one;
}

static const Json *member(const Json *object, const char *name) {
    if (object == NULL || object->kind != JSON_OBJECT) {
        return NULL;
    }
    size_t length = strlen(name);
    for (uint32_t i = 0; i < object->count; i++) {
        if (object->name_lengths[i] == length &&
            memcmp(object->names[i], name, length) == 0) {
            return object->items[i];
        }
    }
    return NULL;
}

static const Json *down(const Json *object, const char *first,
                        const char *second) {
    return member(member(object, first), second);
}

// What this server keeps: one file being worked on, its text, and the last
// build of it. There is one because an editor has one file in front of the
// person using it; a second would be a second build of the same program with
// nothing to say which is current.
typedef struct {
    KestArena *arena;
    const char *library;
    FILE *out;
    char *path;
    char *uri;
    char *text;
    size_t length;
    KestBuild *build;
    bool shutting_down;
} Server;

// A message and its length, which is how LSP frames one. Written into a buffer
// first because the header says how long the body is and a body written
// straight out cannot be measured afterwards. Grown by doubling and written to
// through three calls, because `open_memstream` is not in the standard this is
// held to and is not on every platform this builds for.
typedef struct {
    char *bytes;
    size_t used;
    size_t room;
    bool broke;
} Said;

static void say_bytes(Said *said, const char *bytes, size_t length) {
    if (said->broke) {
        return;
    }
    if (said->used + length + 1 > said->room) {
        size_t grown = said->room == 0 ? 1024 : said->room;
        while (grown < said->used + length + 1) {
            grown *= 2;
        }
        char *moved = realloc(said->bytes, grown);
        if (moved == NULL) {
            said->broke = true;
            return;
        }
        said->bytes = moved;
        said->room = grown;
    }
    memcpy(said->bytes + said->used, bytes, length);
    said->used += length;
    said->bytes[said->used] = '\0';
}

static void say(Said *said, const char *text) {
    say_bytes(said, text, strlen(text));
}

static void say_char(Said *said, char c) {
    say_bytes(said, &c, 1);
}

static void sayf(Said *said, const char *format, ...) KEST_SAYS(2, 3);

static void sayf(Said *said, const char *format, ...) {
    char room[512];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(room, sizeof(room), format, args);
    va_end(args);
    if (written < 0) {
        said->broke = true;
        return;
    }
    if ((size_t)written < sizeof(room)) {
        say_bytes(said, room, (size_t)written);
        return;
    }
    char *wider = malloc((size_t)written + 1);
    if (wider == NULL) {
        said->broke = true;
        return;
    }
    va_start(args, format);
    vsnprintf(wider, (size_t)written + 1, format, args);
    va_end(args);
    say_bytes(said, wider, (size_t)written);
    free(wider);
}

static void let_go(Said *said) {
    free(said->bytes);
    said->bytes = NULL;
    said->used = 0;
    said->room = 0;
}

static void put_escaped(Said *out, const char *text, size_t length) {
    say_char(out, '"');
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        switch (c) {
        case '"': say(out, "\\\""); break;
        case '\\': say(out, "\\\\"); break;
        case '\n': say(out, "\\n"); break;
        case '\r': say(out, "\\r"); break;
        case '\t': say(out, "\\t"); break;
        default:
            if (c < 0x20) {
                sayf(out, "\\u%04x", c);
            } else {
                say_char(out, (char)c);
            }
        }
    }
    say_char(out, '"');
}

static void put_text(Said *out, const char *text) {
    put_escaped(out, text == NULL ? "" : text, text == NULL ? 0 : strlen(text));
}

static void send(Server *server, const char *body, size_t length) {
    fprintf(server->out, "Content-Length: %zu\r\n\r\n", length);
    fwrite(body, 1, length, server->out);
    fflush(server->out);
}

// Where a byte offset is, as a client counts: lines from nought, and columns
// in sixteen-bit units, which is what LSP means by a character unless the
// client says otherwise.
static void locate(const char *text, size_t length, size_t offset,
                   uint32_t *line, uint32_t *column) {
    uint32_t at_line = 0;
    size_t line_start = 0;
    for (size_t i = 0; i < offset && i < length; i++) {
        if (text[i] == '\n') {
            at_line++;
            line_start = i + 1;
        }
    }
    uint32_t units = 0;
    for (size_t i = line_start; i < offset && i < length; ) {
        unsigned char c = (unsigned char)text[i];
        size_t wide = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2
                                 : (c & 0xf0) == 0xe0   ? 3
                                                        : 4;
        units += wide == 4 ? 2 : 1;
        i += wide;
    }
    *line = at_line;
    *column = units;
}

// And the other way: what a client called line and column, as a byte offset.
static size_t offset_of(const char *text, size_t length, uint32_t line,
                        uint32_t column) {
    size_t at = 0;
    for (uint32_t seen = 0; seen < line && at < length; at++) {
        if (text[at] == '\n') {
            seen++;
        }
    }
    uint32_t units = 0;
    while (at < length && text[at] != '\n' && units < column) {
        unsigned char c = (unsigned char)text[at];
        size_t wide = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2
                                 : (c & 0xf0) == 0xe0   ? 3
                                                        : 4;
        units += wide == 4 ? 2 : 1;
        at += wide;
    }
    return at;
}

static void put_range(Said *out, const char *text, size_t length,
                      size_t from, size_t count) {
    uint32_t line = 0;
    uint32_t column = 0;
    locate(text, length, from, &line, &column);
    sayf(out, "{\"start\":{\"line\":%u,\"character\":%u},", line, column);
    locate(text, length, from + count, &line, &column);
    sayf(out, "\"end\":{\"line\":%u,\"character\":%u}}", line, column);
}

// A path out of `file:///...`, with the percent escapes read back. Anything
// that is not a `file:` URI is handed back as it is, which is what a client
// that sends a path rather than a URI wants.
static uint32_t nibble(char c) {
    if (c >= '0' && c <= '9') {
        return (uint32_t)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (uint32_t)(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return (uint32_t)(c - 'A' + 10);
    }
    return 0;
}

// The path is the server's own and outlives every message, because the arena a
// message is read into is rewound the moment it is answered. A path pointing
// into that is a path that reads as whatever the next message wrote there.
static char *path_of_uri(const char *uri, size_t length) {
    const char *from = uri;
    size_t left = length;
    if (left > 7 && memcmp(from, "file://", 7) == 0) {
        from += 7;
        left -= 7;
        // `file:///c:/x` on Windows has a slash before the drive letter that
        // is part of the URI and not part of the path.
        if (left > 2 && from[0] == '/' && from[2] == ':') {
            from++;
            left--;
        }
    }
    char *out = malloc(left + 1);
    if (out == NULL) {
        return NULL;
    }
    size_t used = 0;
    for (size_t i = 0; i < left; i++) {
        if (from[i] == '%' && i + 2 < left) {
            out[used++] = (char)((nibble(from[i + 1]) << 4) |
                                 nibble(from[i + 2]));
            i += 2;
            continue;
        }
        out[used++] = from[i];
    }
    out[used] = '\0';
    return out;
}

static void put_uri(Said *out, const char *path) {
    say(out, "\"file://");
    for (const char *at = path; *at != '\0'; at++) {
        unsigned char c = (unsigned char)*at;
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '/' || c == '.' ||
                     c == '-' || c == '_' || c == '~' || c == ':';
        if (plain) {
            say_char(out, (char)c);
        } else {
            sayf(out, "%%%02X", c);
        }
    }
    say_char(out, '"');
}

// The file a source is, as text this server holds. A build reads every file a
// program imports, so an answer may be about a file that is not the one open.
static const char *text_of(const KestSource *source, size_t *length) {
    if (source == NULL) {
        *length = 0;
        return "";
    }
    *length = source->length;
    return source->text;
}

static void build_again(Server *server) {
    if (server->build != NULL) {
        kest_build_free(server->build);
        server->build = NULL;
    }
    if (server->path == NULL) {
        return;
    }
    kest_loader_overlay(server->path, server->text, server->length);
    char *paths[1] = {server->path};
    server->build = kest_build_open(server->library, paths, 1, 0);
    if (server->build != NULL) {
        kest_build_index_names(server->build, true);
        kest_build_check(server->build);
    }
    kest_loader_overlay(NULL, NULL, 0);
}

// What is wrong with the file, sent whether or not anything is: a client that
// is told nothing leaves the last set on the screen.
static void publish(Server *server) {
    Said out = {0};
    say(&out, "{\"jsonrpc\":\"2.0\",\"method\":"
              "\"textDocument/publishDiagnostics\",\"params\":{\"uri\":");
    put_text(&out, server->uri);
    say(&out, ",\"diagnostics\":[");
    bool first = true;
    if (server->build != NULL) {
        const KestDiags *diags = &server->build->diags;
        for (uint32_t i = 0; i < diags->count; i++) {
            const KestDiag *one = &diags->items[i];
            // Only what is wrong with the file in front of the person. A
            // program is more than one file and a client is told about one.
            if (one->source == NULL || server->path == NULL ||
                strcmp(one->source->path, server->path) != 0) {
                continue;
            }
            size_t length = 0;
            const char *text = text_of(one->source, &length);
            say(&out, first ? "" : ",");
            first = false;
            say(&out, "{\"range\":");
            put_range(&out, text, length, one->span.offset,
                      one->span.length == 0 ? 1 : one->span.length);
            sayf(&out, ",\"severity\":%d,\"source\":\"kest\",\"code\":",
                    one->severity == KEST_SEVERITY_ERROR ? 1 : 2);
            put_text(&out, one->code);
            say(&out, ",\"message\":");
            if (one->suggestion != NULL && one->suggestion[0] != '\0') {
                size_t room = strlen(one->message) +
                              strlen(one->suggestion) + 4;
                char *both = kest_arena_alloc(server->arena, room, 1);
                if (both != NULL) {
                    snprintf(both, room, "%s\n%s", one->message,
                             one->suggestion);
                    put_text(&out, both);
                } else {
                    put_text(&out, one->message);
                }
            } else {
                put_text(&out, one->message);
            }
            say_char(&out, '}');
        }
    }
    say(&out, "]}}");
    if (!out.broke && out.bytes != NULL) {
        send(server, out.bytes, out.used);
    }
    let_go(&out);
}

// Which name is written under this offset, out of the index the checker wrote
// while it resolved. The nearest one that covers it, because a field access is
// a name inside a name.
static const KestUse *use_at(Server *server, size_t offset) {
    if (server->build == NULL || server->build->program == NULL) {
        return NULL;
    }
    uint32_t count = 0;
    const KestUse *uses = kest_program_uses(server->build->program, &count);
    const KestUse *best = NULL;
    for (uint32_t i = 0; i < count; i++) {
        const KestUse *one = &uses[i];
        if (one->source == NULL || server->path == NULL ||
            strcmp(one->source->path, server->path) != 0) {
            continue;
        }
        if (offset < one->span.offset ||
            offset >= (size_t)one->span.offset + one->span.length) {
            continue;
        }
        if (best == NULL || one->span.length < best->span.length) {
            best = one;
        }
    }
    return best;
}

static bool same_declaration(const KestUse *a, const KestUse *b) {
    return a->declared_in == b->declared_in &&
           a->declared.offset == b->declared.offset &&
           a->is_local == b->is_local;
}

static void put_id(Said *out, const Json *id) {
    if (id == NULL) {
        say(out, "null");
    } else if (id->kind == JSON_TEXT) {
        put_escaped(out, id->text, id->length);
    } else {
        sayf(out, "%lld", (long long)id->number);
    }
}

// Every answer is written into a buffer of its own and framed the same way, so
// the framing is written once rather than once per request.
static void start_answer(Said *out, const Json *id) {
    say(out, "{\"jsonrpc\":\"2.0\",\"id\":");
    put_id(out, id);
    say(out, ",\"result\":");
}

static void finish_answer(Server *server, Said *out) {
    say_char(out, '}');
    if (!out->broke && out->bytes != NULL) {
        send(server, out->bytes, out->used);
    }
    let_go(out);
}

static void answer(Server *server, const Json *id, const char *result) {
    Said out = {0};
    start_answer(&out, id);
    say(&out, result);
    finish_answer(server, &out);
}

static size_t position_in(Server *server, const Json *params) {
    const Json *position = member(params, "position");
    const Json *line = member(position, "line");
    const Json *column = member(position, "character");
    if (line == NULL || column == NULL) {
        return (size_t)-1;
    }
    return offset_of(server->text, server->length, (uint32_t)line->number,
                     (uint32_t)column->number);
}

static void hover(Server *server, const Json *id, const Json *params) {
    size_t offset = position_in(server, params);
    const KestUse *one = offset == (size_t)-1 ? NULL : use_at(server, offset);
    Said out = {0};
    start_answer(&out, id);
    if (one == NULL || server->build == NULL) {
        say(&out, "null");
    } else {
        const char *written =
            kest_type_name(server->build->arena, (KestType *)one->type);
        size_t room = strlen(written) + (size_t)one->span.length + 16;
        char *said = kest_arena_alloc(server->arena, room, 1);
        if (said == NULL) {
            say(&out, "null");
        } else {
            snprintf(said, room, "%.*s: %s", (int)one->span.length,
                     kest_span_text(one->source, one->span), written);
            say(&out, "{\"contents\":{\"kind\":\"plaintext\",\"value\":");
            put_text(&out, said);
            say(&out, "},\"range\":");
            put_range(&out, server->text, server->length, one->span.offset,
                      one->span.length);
            say_char(&out, '}');
        }
    }
    finish_answer(server, &out);
}

static void definition(Server *server, const Json *id, const Json *params) {
    size_t offset = position_in(server, params);
    const KestUse *one = offset == (size_t)-1 ? NULL : use_at(server, offset);
    Said out = {0};
    start_answer(&out, id);
    if (one == NULL || one->declared_in == NULL) {
        say(&out, "null");
    } else {
        size_t length = 0;
        const char *text = text_of(one->declared_in, &length);
        say(&out, "{\"uri\":");
        put_uri(&out, one->declared_in->path);
        say(&out, ",\"range\":");
        put_range(&out, text, length, one->declared.offset,
                  one->declared.length);
        say_char(&out, '}');
    }
    finish_answer(server, &out);
}

static void references(Server *server, const Json *id, const Json *params) {
    size_t offset = position_in(server, params);
    const KestUse *wanted = offset == (size_t)-1 ? NULL
                                                 : use_at(server, offset);
    Said out = {0};
    start_answer(&out, id);
    say_char(&out, '[');
    bool first = true;
    if (wanted != NULL && server->build != NULL &&
        server->build->program != NULL) {
        uint32_t count = 0;
        const KestUse *uses = kest_program_uses(server->build->program,
                                                &count);
        for (uint32_t i = 0; i < count; i++) {
            if (!same_declaration(&uses[i], wanted)) {
                continue;
            }
            size_t length = 0;
            const char *text = text_of(uses[i].source, &length);
            say(&out, first ? "" : ",");
            first = false;
            say(&out, "{\"uri\":");
            put_uri(&out, uses[i].source->path);
            say(&out, ",\"range\":");
            put_range(&out, text, length, uses[i].span.offset,
                      uses[i].span.length);
            say_char(&out, '}');
        }
    }
    say_char(&out, ']');
    finish_answer(server, &out);
}

static void rename_everywhere(Server *server, const Json *id,
                              const Json *params) {
    size_t offset = position_in(server, params);
    const KestUse *wanted = offset == (size_t)-1 ? NULL
                                                 : use_at(server, offset);
    const Json *fresh = member(params, "newName");
    Said out = {0};
    start_answer(&out, id);
    if (wanted == NULL || fresh == NULL || fresh->kind != JSON_TEXT ||
        server->build == NULL || server->build->program == NULL) {
        say(&out, "null");
        finish_answer(server, &out);
        return;
    }
    // One file at a time, which is what this server holds. A rename that
    // reaches another file is a rename this says nothing about rather than one
    // it half does.
    say(&out, "{\"changes\":{");
    put_uri(&out, server->path);
    say(&out, ":[");
    bool first = true;
    uint32_t count = 0;
    const KestUse *uses = kest_program_uses(server->build->program, &count);
    for (uint32_t i = 0; i < count; i++) {
        if (!same_declaration(&uses[i], wanted) ||
            uses[i].source == NULL ||
            strcmp(uses[i].source->path, server->path) != 0) {
            continue;
        }
        say(&out, first ? "" : ",");
        first = false;
        say(&out, "{\"range\":");
        put_range(&out, server->text, server->length, uses[i].span.offset,
                  uses[i].span.length);
        say(&out, ",\"newText\":");
        put_escaped(&out, fresh->text, fresh->length);
        say_char(&out, '}');
    }
    // And the declaration itself, when it is in this file.
    if (wanted->declared_in != NULL &&
        strcmp(wanted->declared_in->path, server->path) == 0) {
        say(&out, first ? "" : ",");
        say(&out, "{\"range\":");
        put_range(&out, server->text, server->length, wanted->declared.offset,
                  wanted->declared.length);
        say(&out, ",\"newText\":");
        put_escaped(&out, fresh->text, fresh->length);
        say_char(&out, '}');
    }
    say(&out, "]}}");
    finish_answer(server, &out);
}

// What a file declares, and what every file in the program declares, which are
// the same walk asked of one file or of all of them.
static void symbols(Server *server, const Json *id, bool one_file,
                    const char *query, size_t query_length) {
    Said out = {0};
    start_answer(&out, id);
    say_char(&out, '[');
    bool first = true;
    if (server->build != NULL && server->build->program != NULL) {
        KestProgram *program = server->build->program;
        for (uint32_t i = 0; i < program->global_count; i++) {
            const KestSymbol *symbol = &program->globals[i];
            if (symbol->source == NULL) {
                continue;
            }
            if (one_file && (server->path == NULL ||
                             strcmp(symbol->source->path, server->path) != 0)) {
                continue;
            }
            if (query_length > 0 &&
                strstr(symbol->name, query) == NULL) {
                continue;
            }
            size_t length = 0;
            const char *text = text_of(symbol->source, &length);
            say(&out, first ? "" : ",");
            first = false;
            say(&out, "{\"name\":");
            put_text(&out, symbol->name);
            // Twelve is a function and fourteen is a constant, which is what
            // the two kinds here are.
            // Twelve is a function and fourteen is a constant, which is
            // what the two kinds here are.
            sayf(&out, ",\"kind\":%d,\"location\":{\"uri\":",
                 symbol->type != NULL && symbol->type->tag == KEST_T_FN ? 12
                                                                       : 14);
            put_uri(&out, symbol->source->path);
            say(&out, ",\"range\":");
            put_range(&out, text, length, symbol->span.offset,
                      symbol->span.length);
            say(&out, "}}");
        }
    }
    say_char(&out, ']');
    finish_answer(server, &out);
}

static void completion(Server *server, const Json *id) {
    Said out = {0};
    start_answer(&out, id);
    say(&out, "{\"isIncomplete\":false,\"items\":[");
    bool first = true;
    static const char *const WORDS[] = {
        "break", "const",  "continue", "defer",  "else",  "enum",
        "extern", "false", "fn",       "for",    "if",    "import",
        "in",    "let",    "match",    "module", "none",  "return",
        "struct", "true",  "while",    "scratch", "flags",
        "own",
    };
    for (size_t i = 0; i < sizeof(WORDS) / sizeof(WORDS[0]); i++) {
        say(&out, first ? "" : ",");
        first = false;
        say(&out, "{\"label\":");
        put_text(&out, WORDS[i]);
        say(&out, ",\"kind\":14}");
    }
    if (server->build != NULL && server->build->program != NULL) {
        KestProgram *program = server->build->program;
        for (uint32_t i = 0; i < program->global_count; i++) {
            const KestSymbol *symbol = &program->globals[i];
            say(&out, first ? "" : ",");
            first = false;
            say(&out, "{\"label\":");
            put_text(&out, symbol->name);
            sayf(&out, ",\"kind\":%d,\"detail\":",
                 symbol->type != NULL && symbol->type->tag == KEST_T_FN ? 3
                                                                       : 21);
            put_text(&out,
                     kest_type_name(server->build->arena, symbol->type));
            say_char(&out, '}');
        }
    }
    say(&out, "]}");
    finish_answer(server, &out);
}

// The one form, which is the formatter this tree already has: an editor asking
// for a file to be formatted gets exactly what `kest fmt` writes, because it is
// the same call.
static void formatting(Server *server, const Json *id) {
    Said out = {0};
    start_answer(&out, id);
    // The formatter hands back the file as it should be written, in arena
    // memory, which is the same call `kest fmt` makes: an editor formatting a
    // file gets exactly what the command line would have written.
    const char *formatted = NULL;
    size_t formatted_size = 0;
    if (server->build != NULL && server->build->units.count > 0) {
        formatted = kest_format(&server->build->units.items[0].unit,
                                &server->build->units.items[0].source,
                                server->build->arena, &formatted_size);
    }
    bool worked = formatted != NULL;
    if (!worked || formatted == NULL) {
        say(&out, "null");
    } else {
        say(&out, "[{\"range\":");
        put_range(&out, server->text, server->length, 0, server->length);
        say(&out, ",\"newText\":");
        put_escaped(&out, formatted, formatted_size);
        say(&out, "}]");
    }
    finish_answer(server, &out);
}

static void opened(Server *server, const Json *params) {
    const Json *uri = down(params, "textDocument", "uri");
    const Json *text = down(params, "textDocument", "text");
    if (uri == NULL || uri->kind != JSON_TEXT) {
        return;
    }
    free(server->uri);
    free(server->text);
    free(server->path);
    server->uri = malloc(uri->length + 1);
    if (server->uri != NULL) {
        memcpy(server->uri, uri->text, uri->length);
        server->uri[uri->length] = '\0';
    }
    server->path = path_of_uri(uri->text, uri->length);
    if (text != NULL && text->kind == JSON_TEXT) {
        server->text = malloc(text->length + 1);
        if (server->text != NULL) {
            memcpy(server->text, text->text, text->length);
            server->text[text->length] = '\0';
            server->length = text->length;
        }
    } else {
        server->text = NULL;
        server->length = 0;
    }
    build_again(server);
    publish(server);
}

static void changed(Server *server, const Json *params) {
    const Json *changes = member(params, "contentChanges");
    if (changes == NULL || changes->kind != JSON_LIST || changes->count == 0) {
        return;
    }
    // Whole documents, which is what this server asks for: a range change is
    // an edit this would have to apply itself, and applying an edit twice is
    // the one way a language server can be wrong about what the file says.
    const Json *text = member(changes->items[changes->count - 1], "text");
    if (text == NULL || text->kind != JSON_TEXT) {
        return;
    }
    free(server->text);
    server->text = malloc(text->length + 1);
    if (server->text == NULL) {
        server->length = 0;
        return;
    }
    memcpy(server->text, text->text, text->length);
    server->text[text->length] = '\0';
    server->length = text->length;
    build_again(server);
    publish(server);
}

static bool method_is(const Json *method, const char *name) {
    return method != NULL && method->kind == JSON_TEXT &&
           kest_word_same(name, method->text, method->length);
}

static void handle(Server *server, const Json *message) {
    const Json *method = member(message, "method");
    const Json *id = member(message, "id");
    const Json *params = member(message, "params");

    if (method_is(method, "initialize")) {
        answer(server, id,
               "{\"capabilities\":{"
               "\"textDocumentSync\":1,"
               "\"hoverProvider\":true,"
               "\"definitionProvider\":true,"
               "\"referencesProvider\":true,"
               "\"documentSymbolProvider\":true,"
               "\"workspaceSymbolProvider\":true,"
               "\"renameProvider\":true,"
               "\"documentFormattingProvider\":true,"
               "\"completionProvider\":{\"triggerCharacters\":[\".\"]}"
               "},\"serverInfo\":{\"name\":\"kest\"}}");
        return;
    }
    if (method_is(method, "initialized")) {
        return;
    }
    if (method_is(method, "shutdown")) {
        server->shutting_down = true;
        answer(server, id, "null");
        return;
    }
    if (method_is(method, "textDocument/didOpen")) {
        opened(server, params);
        return;
    }
    if (method_is(method, "textDocument/didChange")) {
        changed(server, params);
        return;
    }
    if (method_is(method, "textDocument/didSave")) {
        build_again(server);
        publish(server);
        return;
    }
    if (method_is(method, "textDocument/didClose")) {
        return;
    }
    if (method_is(method, "textDocument/hover")) {
        hover(server, id, params);
        return;
    }
    if (method_is(method, "textDocument/definition")) {
        definition(server, id, params);
        return;
    }
    if (method_is(method, "textDocument/references")) {
        references(server, id, params);
        return;
    }
    if (method_is(method, "textDocument/documentSymbol")) {
        symbols(server, id, true, "", 0);
        return;
    }
    if (method_is(method, "workspace/symbol")) {
        const Json *query = member(params, "query");
        symbols(server, id, false,
                query != NULL && query->kind == JSON_TEXT ? query->text : "",
                query != NULL && query->kind == JSON_TEXT ? query->length : 0);
        return;
    }
    if (method_is(method, "textDocument/rename")) {
        rename_everywhere(server, id, params);
        return;
    }
    if (method_is(method, "textDocument/completion")) {
        completion(server, id);
        return;
    }
    if (method_is(method, "textDocument/formatting")) {
        formatting(server, id);
        return;
    }
    // A request nothing here answers still gets an answer, because a client
    // waiting on one waits for ever. A notification does not, because nobody
    // is waiting.
    if (id != NULL) {
        answer(server, id, "null");
    }
}

int kest_lsp_serve(const char *library, FILE *in, FILE *out) {
    Server server = {0};
    server.arena = kest_arena_new();
    if (server.arena == NULL) {
        return 1;
    }
    server.library = library;
    server.out = out;

    char header[512];
    while (true) {
        size_t length = 0;
        bool saw_length = false;
        // The header, line by line, to the blank one. Anything that is not a
        // length is skipped: a client may send a content type and this does
        // not care what it says.
        while (fgets(header, sizeof(header), in) != NULL) {
            if (header[0] == '\r' || header[0] == '\n') {
                break;
            }
            unsigned long said = 0;
            if (sscanf(header, "Content-Length: %lu", &said) == 1) {
                length = (size_t)said;
                saw_length = true;
            }
        }
        if (!saw_length || length == 0) {
            break;
        }
        char *body = malloc(length + 1);
        if (body == NULL) {
            break;
        }
        if (fread(body, 1, length, in) != length) {
            free(body);
            break;
        }
        body[length] = '\0';

        // One arena per message, rewound after it: a message is read into a
        // tree and the tree is no use once it is answered.
        KestMark before = kest_arena_mark(server.arena);
        Reading reading = {body, body + length, server.arena, false};
        Json *message = read_value(&reading);
        if (message != NULL && !reading.broke) {
            handle(&server, message);
        }
        kest_arena_rewind(server.arena, before);
        free(body);

        if (server.shutting_down) {
            // `exit` follows `shutdown`, and a client that closes the stream
            // instead is the same thing said another way.
            continue;
        }
    }
    if (server.build != NULL) {
        kest_build_free(server.build);
    }
    free(server.uri);
    free(server.text);
    free(server.path);
    kest_arena_free(server.arena);
    return 0;
}
