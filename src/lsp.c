#include "lsp.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "diag.h"
#include "loader.h"
#include "mem.h"
#include "project.h"
#include "fmt.h"
#include "types.h"
#include "wire.h"

// One file the client has opened: the uri it names the file by, the path the
// rest of this compiler knows it by, and the text as the person has it rather
// than as the disk has it.
typedef struct {
    char *uri;
    char *path;
    char *text;
    size_t length;
} Open;

// What `here` answers when the client has opened nothing, so that a handler
// reads a file rather than a null. Its text is empty and its path is no path,
// which every walk below already asks about.
static char no_text[1] = "";
static Open nothing_open = {NULL, NULL, no_text, 0};

// What this server keeps: every file the client has opened, which one the
// message in hand is about, and the last build of one of them.
//
// There is more than one file because an editor holds more than one open and
// addresses every message to the one it means. The file being typed in is not
// always the one opened last, and a server that kept a single file would write
// a change to one into another: opening A, opening B and then typing in A
// would publish A's mistakes against B's name and leave A looking clean.
//
// There is one build, because building is what every answer is made of and
// building every open file at once is work nobody asked for. `built` says
// which file the build in hand is of, so a request about another rebuilds --
// which costs what a keystroke in that file costs, because a keystroke
// rebuilds too.
typedef struct {
    KestArena *arena;
    const char *library;
    FILE *out;
    Open *open;
    size_t count;
    size_t room;
    size_t at;
    KestBuild *build;
    char *built;
    KestKestFiles files;
    KestResolve resolve;
    // The other files a build read that this server has published something
    // wrong with, so that one published clean later is told it is clean: a
    // client keeps the last list it was sent for a file until it is sent
    // another.
    char **spoken;
    size_t spoken_count;
    size_t spoken_room;
    bool shutting_down;
    // The id of the request being answered, for the answer that could not be
    // written: it is answered all the same, with an error.
    const KestJson *asking;
} Server;

// The file the message in hand is about. `handle` chooses it out of the open
// set before it dispatches anything, and a message naming a file nothing was
// opened for is answered before a handler is reached.
static Open *here(Server *server) {
    return server->at < server->count ? &server->open[server->at]
                                      : &nothing_open;
}

// A copy this server holds on to. Everything a message carries sits in an
// arena that is rewound the moment the message is answered, so a document kept
// between messages is a document copied out of it.
static char *copy_of(const char *text, size_t length) {
    char *held = malloc(length + 1);
    if (held == NULL) {
        return NULL;
    }
    memcpy(held, text, length);
    held[length] = '\0';
    return held;
}

static bool point_at(Server *server, const char *uri, size_t length) {
    for (size_t i = 0; i < server->count; i++) {
        if (server->open[i].uri != NULL &&
            kest_word_same(server->open[i].uri, uri, length)) {
            server->at = i;
            return true;
        }
    }
    server->at = server->count;
    return false;
}

static Open *room_for_another(Server *server) {
    if (server->count == server->room) {
        size_t grown = server->room == 0 ? 4 : server->room * 2;
        Open *moved = realloc(server->open, grown * sizeof(*moved));
        if (moved == NULL) {
            return NULL;
        }
        server->open = moved;
        server->room = grown;
    }
    Open *one = &server->open[server->count++];
    memset(one, 0, sizeof(*one));
    return one;
}

static void send(Server *server, const char *body, size_t length) {
    kest_wire_send(server->out, body, length);
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
    for (size_t i = line_start; i < offset && i < length;) {
        unsigned char c = (unsigned char)text[i];
        size_t wide = c < 0x80             ? 1
                      : (c & 0xe0) == 0xc0 ? 2
                      : (c & 0xf0) == 0xe0 ? 3
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
        size_t wide = c < 0x80             ? 1
                      : (c & 0xe0) == 0xc0 ? 2
                      : (c & 0xf0) == 0xe0 ? 3
                                           : 4;
        units += wide == 4 ? 2 : 1;
        at += wide;
    }
    return at;
}

static void put_range(KestSaid *out, const char *text, size_t length,
                      size_t from, size_t count) {
    uint32_t line = 0;
    uint32_t column = 0;
    locate(text, length, from, &line, &column);
    kest_wire_sayf(out, "{\"start\":{\"line\":%u,\"character\":%u},", line,
                   column);
    locate(text, length, from + count, &line, &column);
    kest_wire_sayf(out, "\"end\":{\"line\":%u,\"character\":%u}}", line,
                   column);
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
    bool file = left > 7 && memcmp(from, "file://", 7) == 0;
    if (file) {
        from += 7;
        left -= 7;
    }
    char *out = malloc(left + 1);
    if (out == NULL) {
        return NULL;
    }
    size_t used = 0;
    for (size_t i = 0; i < left; i++) {
        if (from[i] == '%' && i + 2 < left) {
            out[used++] =
                (char)((nibble(from[i + 1]) << 4) | nibble(from[i + 2]));
            i += 2;
            continue;
        }
        out[used++] = from[i];
    }
    out[used] = '\0';
    // `file:///c:/x` on Windows has a slash before the drive letter that is
    // part of the URI and not part of the path. Looked for once the escapes
    // are read, because VS Code writes the colon as one: `file:///c%3A/x`.
    if (file && used > 2 && out[0] == '/' && out[2] == ':') {
        memmove(out, out + 1, used);
    }
    return out;
}

static void put_uri(KestSaid *out, const char *path) {
    kest_wire_say(out, "\"file://");
    // And the slash back before a drive letter, without which `C:` reads as
    // the URI's host.
    if (path[0] != '\0' && path[1] == ':') {
        kest_wire_char(out, '/');
    }
    for (const char *at = path; *at != '\0'; at++) {
        unsigned char c = (unsigned char)*at;
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '/' || c == '.' ||
                     c == '-' || c == '_' || c == '~' || c == ':';
        if (plain) {
            kest_wire_char(out, (char)c);
        } else {
            kest_wire_sayf(out, "%%%02X", c);
        }
    }
    kest_wire_char(out, '"');
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
    free(server->built);
    server->built = NULL;
    Open *file = here(server);
    if (file->path == NULL) {
        return;
    }
    // Every open file goes in front of the disk, not only the one being built:
    // a file that imports one the person has edited and not saved has to be
    // checked against what they can see rather than against what was saved.
    for (size_t i = 0; i < server->count; i++) {
        if (server->open[i].path != NULL) {
            kest_loader_overlay(server->open[i].path, server->open[i].text,
                                server->open[i].length);
        }
    }
    char *paths[1] = {file->path};
    server->build = kest_build_open(server->library, paths, 1, 0);
    if (server->build != NULL) {
        kest_build_index_names(server->build, true);
        kest_build_check(server->build);
        server->built = copy_of(file->path, strlen(file->path));
    }
    kest_loader_overlay(NULL, NULL, 0);
}

// The build a request is answered out of, which has to be of the file the
// request names. A request about the file last built costs nothing here.
static void ready(Server *server) {
    Open *file = here(server);
    if (server->build != NULL && server->built != NULL && file->path != NULL &&
        strcmp(server->built, file->path) == 0) {
        return;
    }
    build_again(server);
}

// One diagnostic of the list a client is sent, with its suggestion under its
// message as a client shows one.
static void put_said(Server *server, KestSaid *out, bool *first,
                     const char *text, size_t length, KestSpan span, bool error,
                     const char *code, const char *message,
                     const char *suggestion) {
    kest_wire_say(out, *first ? "" : ",");
    *first = false;
    kest_wire_say(out, "{\"range\":");
    put_range(out, text, length, span.offset,
              span.length == 0 ? 1 : span.length);
    kest_wire_sayf(
        out, ",\"severity\":%d,\"source\":\"kest\",\"code\":", error ? 1 : 2);
    kest_wire_text(out, code);
    kest_wire_say(out, ",\"message\":");
    if (suggestion != NULL && suggestion[0] != '\0') {
        size_t room = strlen(message) + strlen(suggestion) + 4;
        char *both = kest_arena_alloc(server->arena, room, 1);
        if (both != NULL) {
            snprintf(both, room, "%s\n%s", message, suggestion);
            kest_wire_text(out, both);
        } else {
            kest_wire_text(out, message);
        }
    } else {
        kest_wire_text(out, message);
    }
    kest_wire_char(out, '}');
}

// A file this server has published something wrong with, kept so that it can
// be told when it is clean. With no room to keep it, it is told now and not
// told later, which is what a client was told before any of this.
static void remember_spoken(Server *server, const char *path) {
    if (server->spoken_count == server->spoken_room) {
        size_t grown = server->spoken_room == 0 ? 8 : server->spoken_room * 2;
        char **bigger = realloc(server->spoken, grown * sizeof(char *));
        if (bigger == NULL) {
            return;
        }
        server->spoken = bigger;
        server->spoken_room = grown;
    }
    char *kept = copy_of(path, strlen(path));
    if (kept != NULL) {
        server->spoken[server->spoken_count++] = kept;
    }
}

// The first mistake in a file the build read, or NULL for one with none.
static const KestDiag *first_error(const KestDiags *diags,
                                   const KestSource *source) {
    for (uint32_t i = 0; i < diags->count; i++) {
        const KestDiag *one = &diags->items[i];
        if (one->source == source && one->severity == KEST_SEVERITY_ERROR &&
            kest_diags_worth_saying(diags, one)) {
            return one;
        }
    }
    return NULL;
}

// One note on the open file for each module it reaches that does not build:
// on the line that imports it where the file does, and at its start where the
// module is reached through another, with that module's first mistake.
// The unit a module name is, or the count when no unit is that module.
static uint32_t unit_named(const KestUnits *units, const char *text,
                           size_t length) {
    for (uint32_t k = 0; k < units->count; k++) {
        if (kest_word_same(units->items[k].module, text, length)) {
            return k;
        }
    }
    return units->count;
}

// Whether one unit reaches another through what it imports, and what those
// import. Walked with a list of its own rather than by calling itself, since
// how deep a program's imports go is the program's to say. No room for the
// walk answers no, and the note it was asked for goes at the top of the file.
static bool leads_to(Server *server, uint32_t from, uint32_t to) {
    const KestUnits *units = &server->build->units;
    if (from >= units->count) {
        return false;
    }
    uint32_t *waiting = KEST_ARENA_ARRAY(server->arena, uint32_t, units->count);
    bool *seen = KEST_ARENA_ARRAY(server->arena, bool, units->count);
    if (waiting == NULL || seen == NULL) {
        return false;
    }
    uint32_t count = 0;
    waiting[count++] = from;
    seen[from] = true;
    while (count > 0) {
        const KestUnitInfo *at = &units->items[waiting[--count]];
        if (at == &units->items[to]) {
            return true;
        }
        for (uint32_t i = 0; i < at->import_count; i++) {
            const char *module = at->import_paths[i];
            uint32_t next = unit_named(units, module, strlen(module));
            if (next < units->count && !seen[next]) {
                seen[next] = true;
                waiting[count++] = next;
            }
        }
    }
    return false;
}

static void imports_failing(Server *server, KestSaid *out, bool *first,
                            const Open *file) {
    const KestUnits *units = &server->build->units;
    const KestUnitInfo *open = NULL;
    for (uint32_t k = 0; k < units->count; k++) {
        if (strcmp(units->items[k].source.path, file->path) == 0) {
            open = &units->items[k];
        }
    }
    if (open == NULL) {
        return;
    }
    for (uint32_t k = 0; k < units->count; k++) {
        const KestUnitInfo *other = &units->items[k];
        const KestDiag *wrong =
            other == open ? NULL
                          : first_error(&server->build->diags, &other->source);
        if (wrong == NULL) {
            continue;
        }
        // On the import that names it, or else on the first one that leads
        // to it: a module read for a module the file imports is one the file
        // never names, and its import line is where the file meets it.
        KestSpan where = {0, 0};
        for (uint32_t d = 0; d < open->unit.count; d++) {
            const KestDecl *decl = open->unit.items[d];
            if (decl->kind != KEST_DECL_IMPORT) {
                continue;
            }
            const char *text = kest_span_text(&open->source, decl->name);
            if (kest_word_same(other->module, text, decl->name.length)) {
                where = decl->name;
                break;
            }
            if (where.length == 0 &&
                leads_to(server, unit_named(units, text, decl->name.length),
                         k)) {
                where = decl->name;
            }
        }
        const char *named =
            other->module[0] != '\0' ? other->module : other->source.path;
        size_t room = strlen(named) + strlen(wrong->message) + 32;
        char *said = kest_arena_alloc(server->arena, room, 1);
        if (said == NULL) {
            continue;
        }
        snprintf(said, room, "`%s` does not build: %s", named, wrong->message);
        put_said(server, out, first, file->text, file->length, where, true,
                 wrong->code, said, NULL);
    }
}

// What is wrong with one other file the build read, sent to that file.
static void publish_to(Server *server, const char *path,
                       const KestSource *source) {
    KestSaid out = {0};
    kest_wire_say(&out,
                  "{\"jsonrpc\":\"2.0\",\"method\":"
                  "\"textDocument/publishDiagnostics\",\"params\":{\"uri\":");
    put_uri(&out, path);
    kest_wire_say(&out, ",\"diagnostics\":[");
    bool first = true;
    if (source != NULL) {
        const KestDiags *diags = &server->build->diags;
        for (uint32_t i = 0; i < diags->count; i++) {
            const KestDiag *one = &diags->items[i];
            if (one->source != source || !kest_diags_worth_saying(diags, one)) {
                continue;
            }
            put_said(server, &out, &first, source->text, source->length,
                     one->span, one->severity == KEST_SEVERITY_ERROR, one->code,
                     one->message, one->suggestion);
        }
    }
    kest_wire_say(&out, "]}}");
    if (!out.broke && out.bytes != NULL) {
        send(server, out.bytes, out.used);
    }
    kest_wire_let_go(&out);
}

// Whether a path names a file the client has open. An open file is told by
// its own build, which says more about it than this one can -- the modules it
// imports that do not build, what is about no file -- and a list sent from
// here would put only its own lines in place of all of that.
static bool is_open(const Server *server, const char *path) {
    for (size_t i = 0; i < server->count; i++) {
        if (server->open[i].path != NULL &&
            strcmp(server->open[i].path, path) == 0) {
            return true;
        }
    }
    return false;
}

// Whether a path can be written as a URI a client finds its file by: one from
// the root, or from a drive. Every file a build reads is one when the open
// file's path is, since a project's roots are found from that path and the
// command line hands this server its library from the root. A buffer a client
// names in another scheme reads its imports from where the server runs, and
// those are left untold rather than named as a URI that names nothing.
static bool from_the_root(const char *path) {
    return path[0] == '/' || (path[0] != '\0' && path[1] == ':' &&
                              (path[2] == '/' || path[2] == '\\'));
}

// Every other file the build read, told what is wrong with it, and every one
// this server told something before and the build now reads clean, told that.
// A file the build did not read is left as it was told: another build said
// that, and this one knows nothing new about it.
static void publish_others(Server *server, const Open *file) {
    if (server->build == NULL || file->path == NULL) {
        return;
    }
    const KestUnits *units = &server->build->units;
    for (uint32_t k = 0; k < units->count; k++) {
        const KestSource *source = &units->items[k].source;
        if (strcmp(source->path, file->path) == 0 ||
            is_open(server, source->path) || !from_the_root(source->path)) {
            continue;
        }
        bool wrong = false;
        for (uint32_t i = 0; i < server->build->diags.count && !wrong; i++) {
            const KestDiag *one = &server->build->diags.items[i];
            wrong = one->source == source &&
                    kest_diags_worth_saying(&server->build->diags, one);
        }
        size_t told = server->spoken_count;
        for (size_t s = 0; s < server->spoken_count; s++) {
            if (strcmp(server->spoken[s], source->path) == 0) {
                told = s;
            }
        }
        if (wrong) {
            publish_to(server, source->path, source);
            if (told == server->spoken_count) {
                remember_spoken(server, source->path);
            }
        } else if (told < server->spoken_count) {
            publish_to(server, source->path, NULL);
            free(server->spoken[told]);
            server->spoken[told] = server->spoken[--server->spoken_count];
        }
    }
}

// What is wrong with the file, sent whether or not anything is: a client that
// is told nothing leaves the last set on the screen.
static void publish(Server *server) {
    Open *file = here(server);
    KestSaid out = {0};
    kest_wire_say(&out,
                  "{\"jsonrpc\":\"2.0\",\"method\":"
                  "\"textDocument/publishDiagnostics\",\"params\":{\"uri\":");
    kest_wire_text(&out, file->uri);
    kest_wire_say(&out, ",\"diagnostics\":[");
    bool first = true;
    if (server->build != NULL && file->path != NULL) {
        const KestDiags *diags = &server->build->diags;
        KestSpan start = {0, 0};
        for (uint32_t i = 0; i < diags->count; i++) {
            const KestDiag *one = &diags->items[i];
            // Only what is wrong with the file in front of the person. A
            // program is more than one file and a client is told about one.
            // What is about no file -- a manifest that cannot be read -- is
            // about this one too, and is put at its start: left out, a file
            // in a project nothing could be read of was shown as one with
            // nothing wrong with it.
            if (!kest_diags_worth_saying(diags, one) ||
                (one->source != NULL &&
                 strcmp(one->source->path, file->path) != 0)) {
                continue;
            }
            size_t length = file->length;
            const char *text = file->text;
            KestSpan span = start;
            if (one->source != NULL) {
                text = text_of(one->source, &length);
                span = one->span;
            }
            put_said(server, &out, &first, text, length, span,
                     one->severity == KEST_SEVERITY_ERROR, one->code,
                     one->message, one->suggestion);
        }
        // And running out, which is a bit beside the list rather than an
        // item in it: a build that ran out published only what it worked out
        // before it did, which can be nothing, and a file that could not be
        // checked was shown as one with nothing wrong with it. Said as the
        // command line says it, what it was about to say first.
        char room[160];
        const char *code = NULL;
        const char *words =
            kest_diags_starved_words(diags, &code, room, sizeof room);
        if (words != NULL) {
            if (diags->last_code[0] != '\0') {
                put_said(server, &out, &first, file->text, file->length, start,
                         true, diags->last_code, diags->last_words, NULL);
            }
            put_said(server, &out, &first, file->text, file->length, start,
                     true, code, words, NULL);
        }
        // And every module the file reaches that does not build, said where
        // the file imports it: a file whose own lines are fine and whose
        // program is not was shown clean while `kest check` refused it.
        imports_failing(server, &out, &first, file);
    } else if (file->path != NULL) {
        // No build at all, which is a build that had no room to begin: said
        // as the command line says it, rather than as a file with nothing
        // wrong with it.
        KestSpan start = {0, 0};
        put_said(server, &out, &first, file->text, file->length, start, true,
                 "K0705", "there is not enough memory to read a program", NULL);
    }
    kest_wire_say(&out, "]}}");
    if (!out.broke && out.bytes != NULL) {
        send(server, out.bytes, out.used);
    }
    kest_wire_let_go(&out);
    publish_others(server, file);
}

static bool covers(KestSpan span, size_t offset) {
    return offset >= span.offset && offset < (size_t)span.offset + span.length;
}

// Which name is written under this offset, out of the index the checker wrote
// while it resolved. The nearest one that covers it, because a field access is
// a name inside a name. And where no use is written there, a use of what is
// declared there: the index holds the places a name is used, a declaration is
// not one of them, and a rename or a reference list asked from the `n` of
// `let n = 1` was answered with nothing.
static const KestUse *use_at(Server *server, size_t offset) {
    if (server->build == NULL || server->build->program == NULL) {
        return NULL;
    }
    uint32_t count = 0;
    const KestUse *uses = kest_program_uses(server->build->program, &count);
    const KestUse *best = NULL;
    const KestUse *declaring = NULL;
    const Open *file = here(server);
    if (file->path == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < count; i++) {
        const KestUse *one = &uses[i];
        if (one->declared_in != NULL && one->declared_in->path != NULL &&
            strcmp(one->declared_in->path, file->path) == 0 &&
            covers(one->declared, offset) &&
            (declaring == NULL ||
             one->declared.length < declaring->declared.length)) {
            declaring = one;
        }
        if (one->source == NULL || strcmp(one->source->path, file->path) != 0 ||
            !covers(one->span, offset)) {
            continue;
        }
        if (best == NULL || one->span.length < best->span.length) {
            best = one;
        }
    }
    return best != NULL ? best : declaring;
}

static bool same_declaration(const KestUse *a, const KestUse *b) {
    return a->declared_in == b->declared_in &&
           a->declared.offset == b->declared.offset &&
           a->is_local == b->is_local;
}

static void put_id(KestSaid *out, const KestJson *id) {
    if (id == NULL) {
        kest_wire_say(out, "null");
    } else if (id->kind == KEST_JSON_TEXT) {
        kest_wire_escaped(out, id->text, id->length);
    } else {
        kest_wire_sayf(out, "%lld", (long long)id->number);
    }
}

// Every answer is written into a buffer of its own and framed the same way, so
// the framing is written once rather than once per request.
static void start_answer(KestSaid *out, const KestJson *id) {
    kest_wire_say(out, "{\"jsonrpc\":\"2.0\",\"id\":");
    put_id(out, id);
    kest_wire_say(out, ",\"result\":");
}

// What a client is told when there was no memory to write what it asked
// for, or to read what it sent, written where no memory is needed. A client
// that sent a request waits for its answer, so one goes whatever happened.
#define NO_ROOM_ERROR                                                          \
    "\"error\":{\"code\":-32603,\"message\":\"" KEST_STARVED_CODE              \
    ": " KEST_STARVED_SAYS "\"}}"

static const char NO_ROOM_TO_READ[] =
    "{\"jsonrpc\":\"2.0\",\"id\":null," NO_ROOM_ERROR;
static const char NOT_JSON[] =
    "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,"
    "\"message\":\"this message is not JSON\"}}";

// The id is written back as it came only when it needs no escaping and fits;
// a client's ids are numbers or short names, and one that is neither is
// answered with no id, which is what JSON-RPC answers an id it cannot read.
static void answer_without_room(Server *server, const KestJson *id) {
    char said[256];
    int written = -1;
    if (id != NULL && id->kind == KEST_JSON_NUMBER) {
        written = snprintf(said, sizeof said,
                           "{\"jsonrpc\":\"2.0\",\"id\":%lld," NO_ROOM_ERROR,
                           (long long)id->number);
    } else if (id != NULL && id->kind == KEST_JSON_TEXT && id->length <= 64) {
        bool plain = true;
        for (size_t i = 0; i < id->length; i++) {
            char c = id->text[i];
            plain = plain && c >= ' ' && c <= '~' && c != '"' && c != '\\';
        }
        if (plain) {
            written =
                snprintf(said, sizeof said,
                         "{\"jsonrpc\":\"2.0\",\"id\":\"%.*s\"," NO_ROOM_ERROR,
                         (int)id->length, id->text);
        }
    }
    if (written < 0 || (size_t)written >= sizeof said) {
        send(server, NO_ROOM_TO_READ, sizeof NO_ROOM_TO_READ - 1);
        return;
    }
    send(server, said, (size_t)written);
}

// A message nested past what is read is answered as one that is not JSON,
// since nothing in it was read, with the number it went past.
static void too_deep(Server *server) {
    char said[192];
    int written = snprintf(said, sizeof said,
                           "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{"
                           "\"code\":-32700,\"message\":\"this message nests "
                           "more than %d deep\"}}",
                           KEST_WIRE_DEEPEST);
    if (written > 0 && (size_t)written < sizeof said) {
        send(server, said, (size_t)written);
    }
}

static void finish_answer(Server *server, KestSaid *out) {
    kest_wire_char(out, '}');
    if (!out->broke && out->bytes != NULL) {
        send(server, out->bytes, out->used);
    } else {
        answer_without_room(server, server->asking);
    }
    kest_wire_let_go(out);
}

static void answer(Server *server, const KestJson *id, const char *result) {
    KestSaid out = {0};
    start_answer(&out, id);
    kest_wire_say(&out, result);
    finish_answer(server, &out);
}

static size_t position_in(Server *server, const KestJson *params) {
    const KestJson *position = kest_wire_member(params, "position");
    const KestJson *line = kest_wire_member(position, "line");
    const KestJson *column = kest_wire_member(position, "character");
    if (line == NULL || column == NULL) {
        return (size_t)-1;
    }
    Open *file = here(server);
    return offset_of(file->text, file->length, (uint32_t)line->number,
                     (uint32_t)column->number);
}

static void hover(Server *server, const KestJson *id, const KestJson *params) {
    size_t offset = position_in(server, params);
    const KestUse *one = offset == (size_t)-1 ? NULL : use_at(server, offset);
    KestSaid out = {0};
    start_answer(&out, id);
    if (one == NULL || server->build == NULL) {
        kest_wire_say(&out, "null");
    } else {
        const char *written =
            kest_type_name(server->build->arena, (KestType *)one->type);
        size_t room = strlen(written) + (size_t)one->span.length + 16;
        char *said = kest_arena_alloc(server->arena, room, 1);
        if (said == NULL) {
            kest_wire_say(&out, "null");
        } else {
            snprintf(said, room, "%.*s: %s", (int)one->span.length,
                     kest_span_text(one->source, one->span), written);
            kest_wire_say(&out,
                          "{\"contents\":{\"kind\":\"plaintext\",\"value\":");
            kest_wire_text(&out, said);
            kest_wire_say(&out, "},\"range\":");
            put_range(&out, here(server)->text, here(server)->length,
                      one->span.offset, one->span.length);
            kest_wire_char(&out, '}');
        }
    }
    finish_answer(server, &out);
}

static void definition(Server *server, const KestJson *id,
                       const KestJson *params) {
    size_t offset = position_in(server, params);
    const KestUse *one = offset == (size_t)-1 ? NULL : use_at(server, offset);
    KestSaid out = {0};
    start_answer(&out, id);
    if (one == NULL || one->declared_in == NULL) {
        kest_wire_say(&out, "null");
    } else {
        size_t length = 0;
        const char *text = text_of(one->declared_in, &length);
        kest_wire_say(&out, "{\"uri\":");
        put_uri(&out, one->declared_in->path);
        kest_wire_say(&out, ",\"range\":");
        put_range(&out, text, length, one->declared.offset,
                  one->declared.length);
        kest_wire_char(&out, '}');
    }
    finish_answer(server, &out);
}

// A request this server will not answer as it was asked, said as one that
// failed (-32803), which a client shows the person instead of acting on.
static void refuse(Server *server, const KestJson *id, const char *why) {
    KestSaid out = {0};
    kest_wire_say(&out, "{\"jsonrpc\":\"2.0\",\"id\":");
    put_id(&out, id);
    kest_wire_say(&out, ",\"error\":{\"code\":-32803,\"message\":");
    kest_wire_text(&out, why);
    kest_wire_char(&out, '}');
    finish_answer(server, &out);
}

// What an index short of a use is told, which is whatever asks it for every
// use of a name: what it would answer is some of them.
#define USES_SHORT                                                             \
    "there was no room to keep every place a name is written, so this "        \
    "would answer some of them"

static void references(Server *server, const KestJson *id,
                       const KestJson *params) {
    size_t offset = position_in(server, params);
    if (server->build != NULL && server->build->program != NULL &&
        server->build->program->uses_short) {
        refuse(server, id, USES_SHORT);
        return;
    }
    const KestUse *wanted =
        offset == (size_t)-1 ? NULL : use_at(server, offset);
    KestSaid out = {0};
    start_answer(&out, id);
    kest_wire_char(&out, '[');
    bool first = true;
    // The declaration is a place the name is written, and a client asking
    // with `includeDeclaration` is asking for it as well.
    const KestJson *context = kest_wire_member(params, "context");
    const KestJson *declaration =
        context == NULL ? NULL
                        : kest_wire_member(context, "includeDeclaration");
    if (wanted != NULL && declaration != NULL &&
        declaration->kind == KEST_JSON_TRUE && wanted->declared_in != NULL &&
        wanted->declared_in->path != NULL) {
        size_t length = 0;
        const char *text = text_of(wanted->declared_in, &length);
        first = false;
        kest_wire_say(&out, "{\"uri\":");
        put_uri(&out, wanted->declared_in->path);
        kest_wire_say(&out, ",\"range\":");
        put_range(&out, text, length, wanted->declared.offset,
                  wanted->declared.length);
        kest_wire_char(&out, '}');
    }
    if (wanted != NULL && server->build != NULL &&
        server->build->program != NULL) {
        uint32_t count = 0;
        const KestUse *uses = kest_program_uses(server->build->program, &count);
        for (uint32_t i = 0; i < count; i++) {
            if (!same_declaration(&uses[i], wanted)) {
                continue;
            }
            size_t length = 0;
            const char *text = text_of(uses[i].source, &length);
            kest_wire_say(&out, first ? "" : ",");
            first = false;
            kest_wire_say(&out, "{\"uri\":");
            put_uri(&out, uses[i].source->path);
            kest_wire_say(&out, ",\"range\":");
            put_range(&out, text, length, uses[i].span.offset,
                      uses[i].span.length);
            kest_wire_char(&out, '}');
        }
    }
    kest_wire_char(&out, ']');
    finish_answer(server, &out);
}

// The files a host listed, kept in the arena a message is answered in.
typedef struct {
    KestArena *arena;
    const char **paths;
    uint32_t count;
    uint32_t room;
    bool short_of_room;
} Listed;

static void listed_one(void *context, const char *path) {
    Listed *listed = context;
    if (listed->short_of_room) {
        return;
    }
    if (listed->count == listed->room) {
        uint32_t room = listed->room == 0 ? 32 : listed->room * 2;
        const char **grown =
            KEST_ARENA_ARRAY(listed->arena, const char *, room);
        if (grown == NULL) {
            listed->short_of_room = true;
            return;
        }
        if (listed->count > 0) {
            memcpy(grown, listed->paths, listed->count * sizeof(const char *));
        }
        listed->paths = grown;
        listed->room = room;
    }
    const char *kept = kest_arena_strndup(listed->arena, path, strlen(path));
    if (kept == NULL) {
        listed->short_of_room = true;
        return;
    }
    listed->paths[listed->count++] = kept;
}

static bool under_directory(const char *path, const char *directory) {
    size_t length = strlen(directory);
    if (length == 0 || strncmp(path, directory, length) != 0) {
        return false;
    }
    return directory[length - 1] == '/' || path[length] == '/';
}

// Every file of the project the open file is in, built as one program with
// its index kept. NULL with `why` said when that cannot be had whole: a rename
// made from part of a project leaves the rest naming what is not there. A
// file in no project is refused too, because nothing says which other files
// are its program's: two files importing a third are each a program, and a
// rename made from one leaves the other naming what is not there.
static KestBuild *whole_project(Server *server, const char **project,
                                const char **why) {
    Open *file = here(server);
    *project = NULL;
    const char *roots[KEST_MOST_SOURCES + 1];
    uint32_t root_count = 0;
    if (!kest_project_above(server->arena, file->path, project, roots,
                            &root_count)) {
        *project = NULL;
        *why = "this file is in no project, so the files that name this "
               "cannot be known; a `kest.project` beside it says which they "
               "are";
        return NULL;
    }
    if (server->resolve == NULL) {
        *why = "this server cannot follow a path's links, so it cannot tell "
               "what is inside the project";
        return NULL;
    }
    if (server->files == NULL) {
        *why = "this server cannot list a project's files, so it cannot find "
               "every file that names this";
        return NULL;
    }
    Listed listed = {server->arena, NULL, 0, 0, false};
    listed_one(&listed, file->path);
    for (uint32_t r = 0; r < root_count; r++) {
        if (!server->files(roots[r], listed_one, &listed)) {
            *why = "a directory of this project could not be listed, so not "
                   "every file that names this could be found";
            return NULL;
        }
    }
    if (listed.short_of_room) {
        *why = USES_SHORT;
        return NULL;
    }
    // The open file first, because the first file names the root everything
    // else resolves from, and once each.
    char **paths = KEST_ARENA_ARRAY(server->arena, char *, listed.count);
    if (paths == NULL) {
        *why = USES_SHORT;
        return NULL;
    }
    int count = 0;
    for (uint32_t i = 0; i < listed.count; i++) {
        bool again = false;
        for (int j = 0; j < count && !again; j++) {
            again = strcmp(paths[j], listed.paths[i]) == 0;
        }
        if (!again) {
            paths[count++] = (char *)listed.paths[i];
        }
    }
    for (size_t i = 0; i < server->count; i++) {
        if (server->open[i].path != NULL) {
            kest_loader_overlay(server->open[i].path, server->open[i].text,
                                server->open[i].length);
        }
    }
    KestBuild *whole = kest_build_open(server->library, paths, count, 0);
    if (whole != NULL) {
        kest_build_index_names(whole, true);
        kest_build_check(whole);
    }
    kest_loader_overlay(NULL, NULL, 0);
    if (whole == NULL || whole->program == NULL || whole->diags.starved) {
        if (whole != NULL) {
            kest_build_free(whole);
        }
        *why = "there was no room to read the whole project, so not every "
               "place that names this could be found";
        return NULL;
    }
    if (whole->diags.error_count > 0) {
        kest_build_free(whole);
        *why = "this project does not build, so not every place that names "
               "this can be found";
        return NULL;
    }
    return whole;
}

// The same place in another build: what is matched is the file it is in, by
// name, and where in it.
static bool declared_at(const KestUse *one, const char *path, uint32_t offset) {
    return !one->is_local && one->declared_in != NULL &&
           one->declared_in->path != NULL &&
           strcmp(one->declared_in->path, path) == 0 &&
           one->declared.offset == offset;
}

// One file's edits: every use of the declaration in it, and the declaration
// when it is there.
static void rename_in(KestSaid *out, const KestUse *uses, uint32_t count,
                      const KestSource *source, const char *path,
                      uint32_t offset, KestSpan declared,
                      const KestJson *fresh) {
    size_t length = 0;
    const char *text = text_of(source, &length);
    put_uri(out, source->path);
    kest_wire_say(out, ":[");
    bool first = true;
    for (uint32_t i = 0; i < count; i++) {
        if (uses[i].source != source || !declared_at(&uses[i], path, offset)) {
            continue;
        }
        kest_wire_say(out, first ? "" : ",");
        first = false;
        kest_wire_say(out, "{\"range\":");
        put_range(out, text, length, uses[i].span.offset, uses[i].span.length);
        kest_wire_say(out, ",\"newText\":");
        kest_wire_escaped(out, fresh->text, fresh->length);
        kest_wire_char(out, '}');
    }
    if (strcmp(source->path, path) == 0) {
        kest_wire_say(out, first ? "" : ",");
        kest_wire_say(out, "{\"range\":");
        put_range(out, text, length, declared.offset, declared.length);
        kest_wire_say(out, ",\"newText\":");
        kest_wire_escaped(out, fresh->text, fresh->length);
        kest_wire_char(out, '}');
    }
    kest_wire_char(out, ']');
}

// Whether a path is inside a directory once every link in both is followed.
// What a rename may change is decided here and not on the spelling: a link in
// the project leads wherever it was pointed, and a file reached through one
// may be a checkout other projects share.
static bool really_inside(Server *server, const char *path,
                          const char *directory) {
    char at[4096];
    return server->resolve(path, at, sizeof(at)) &&
           under_directory(at, directory);
}

// A name more than one file may write: every file of the project that names
// it, the declaration's included, in one edit across them. Never some of
// them, because a rename that leaves one file naming the old word is a
// program that no longer compiles; what cannot be had whole is refused, and
// so is a declaration, or a file that names it, somewhere the person cannot
// change -- the standard library, or anywhere outside the project.
static void rename_across(Server *server, const KestJson *id,
                          const KestUse *wanted, const KestJson *fresh) {
    const char *path = wanted->declared_in->path;
    uint32_t offset = wanted->declared.offset;
    KestSpan declared = wanted->declared;
    if (server->library != NULL && under_directory(path, server->library)) {
        refuse(server, id,
               "this is declared in the standard library, which a rename "
               "does not change");
        return;
    }
    const char *project = NULL;
    const char *why = NULL;
    KestBuild *whole = whole_project(server, &project, &why);
    if (whole == NULL) {
        refuse(server, id, why);
        return;
    }
    char root[4096];
    if (!server->resolve(project, root, sizeof(root))) {
        kest_build_free(whole);
        refuse(server, id,
               "where this project really is could not be said, so neither "
               "could what is inside it");
        return;
    }
    if (!really_inside(server, path, root)) {
        kest_build_free(whole);
        refuse(server, id,
               "this is declared outside the project, which a rename does "
               "not change");
        return;
    }
    if (whole->program->uses_short) {
        kest_build_free(whole);
        refuse(server, id, USES_SHORT);
        return;
    }
    uint32_t count = 0;
    const KestUse *uses = kest_program_uses(whole->program, &count);
    // Each file once, in the order its first edit is met, and the declaring
    // file whether or not anything in it names what it declares.
    const KestSource **files =
        KEST_ARENA_ARRAY(server->arena, const KestSource *, count + 1);
    if (files == NULL) {
        kest_build_free(whole);
        refuse(server, id, USES_SHORT);
        return;
    }
    uint32_t file_count = 0;
    bool declaring = false;
    for (uint32_t i = 0; i < count; i++) {
        if (!declared_at(&uses[i], path, offset)) {
            continue;
        }
        const KestSource *candidates[2] = {uses[i].source, uses[i].declared_in};
        for (int c = 0; c < 2; c++) {
            bool seen = false;
            for (uint32_t f = 0; f < file_count && !seen; f++) {
                seen = files[f] == candidates[c] ||
                       strcmp(files[f]->path, candidates[c]->path) == 0;
            }
            if (!seen) {
                files[file_count++] = candidates[c];
            }
            declaring = declaring || strcmp(candidates[c]->path, path) == 0;
        }
    }
    if (!declaring) {
        kest_build_free(whole);
        refuse(server, id,
               "the file that declares this was not part of the project's "
               "build, so the rename would leave it as it is");
        return;
    }
    for (uint32_t f = 0; f < file_count; f++) {
        if (!really_inside(server, files[f]->path, root)) {
            kest_build_free(whole);
            refuse(server, id,
                   "a file that names this is outside the project, which a "
                   "rename does not change");
            return;
        }
    }
    KestSaid out = {0};
    start_answer(&out, id);
    kest_wire_say(&out, "{\"changes\":{");
    for (uint32_t f = 0; f < file_count; f++) {
        kest_wire_say(&out, f == 0 ? "" : ",");
        rename_in(&out, uses, count, files[f], path, offset, declared, fresh);
    }
    kest_wire_say(&out, "}}");
    kest_build_free(whole);
    finish_answer(server, &out);
}

static void rename_everywhere(Server *server, const KestJson *id,
                              const KestJson *params) {
    size_t offset = position_in(server, params);
    if (server->build != NULL && server->build->program != NULL &&
        server->build->program->uses_short) {
        refuse(server, id, USES_SHORT);
        return;
    }
    const KestUse *wanted =
        offset == (size_t)-1 ? NULL : use_at(server, offset);
    const KestJson *fresh = kest_wire_member(params, "newName");
    if (wanted == NULL || fresh == NULL || fresh->kind != KEST_JSON_TEXT ||
        server->build == NULL || server->build->program == NULL) {
        KestSaid out = {0};
        start_answer(&out, id);
        kest_wire_say(&out, "null");
        finish_answer(server, &out);
        return;
    }
    if (!wanted->is_local && wanted->declared_in != NULL &&
        wanted->declared_in->path != NULL) {
        rename_across(server, id, wanted, fresh);
        return;
    }
    // A local is in one body, and a body is in the file the request named.
    KestSaid out = {0};
    start_answer(&out, id);
    Open *file = here(server);
    kest_wire_say(&out, "{\"changes\":{");
    put_uri(&out, file->path);
    kest_wire_say(&out, ":[");
    bool first = true;
    uint32_t count = 0;
    const KestUse *uses = kest_program_uses(server->build->program, &count);
    for (uint32_t i = 0; i < count; i++) {
        if (!same_declaration(&uses[i], wanted) || uses[i].source == NULL ||
            file->path == NULL ||
            strcmp(uses[i].source->path, file->path) != 0) {
            continue;
        }
        kest_wire_say(&out, first ? "" : ",");
        first = false;
        kest_wire_say(&out, "{\"range\":");
        put_range(&out, file->text, file->length, uses[i].span.offset,
                  uses[i].span.length);
        kest_wire_say(&out, ",\"newText\":");
        kest_wire_escaped(&out, fresh->text, fresh->length);
        kest_wire_char(&out, '}');
    }
    // And the declaration itself, when it is in this file.
    if (wanted->declared_in != NULL && file->path != NULL &&
        strcmp(wanted->declared_in->path, file->path) == 0) {
        kest_wire_say(&out, first ? "" : ",");
        kest_wire_say(&out, "{\"range\":");
        put_range(&out, file->text, file->length, wanted->declared.offset,
                  wanted->declared.length);
        kest_wire_say(&out, ",\"newText\":");
        kest_wire_escaped(&out, fresh->text, fresh->length);
        kest_wire_char(&out, '}');
    }
    kest_wire_say(&out, "]}}");
    finish_answer(server, &out);
}

// What a file declares, and what every file in the program declares, which are
// the same walk asked of one file or of all of them.
static void symbols(Server *server, const KestJson *id, bool one_file,
                    const char *query, size_t query_length) {
    KestSaid out = {0};
    start_answer(&out, id);
    kest_wire_char(&out, '[');
    bool first = true;
    if (server->build != NULL && server->build->program != NULL) {
        KestProgram *program = server->build->program;
        for (uint32_t i = 0; i < program->global_count; i++) {
            const KestSymbol *symbol = &program->globals[i];
            if (symbol->source == NULL) {
                continue;
            }
            if (one_file &&
                (here(server)->path == NULL ||
                 strcmp(symbol->source->path, here(server)->path) != 0)) {
                continue;
            }
            if (query_length > 0 && strstr(symbol->name, query) == NULL) {
                continue;
            }
            size_t length = 0;
            const char *text = text_of(symbol->source, &length);
            kest_wire_say(&out, first ? "" : ",");
            first = false;
            kest_wire_say(&out, "{\"name\":");
            kest_wire_text(&out, symbol->name);
            // Twelve is a function and fourteen is a constant, which is what
            // the two kinds here are.
            // Twelve is a function and fourteen is a constant, which is
            // what the two kinds here are.
            kest_wire_sayf(
                &out, ",\"kind\":%d,\"location\":{\"uri\":",
                symbol->type != NULL && symbol->type->tag == KEST_T_FN ? 12
                                                                       : 14);
            put_uri(&out, symbol->source->path);
            kest_wire_say(&out, ",\"range\":");
            put_range(&out, text, length, symbol->span.offset,
                      symbol->span.length);
            kest_wire_say(&out, "}}");
        }
    }
    kest_wire_char(&out, ']');
    finish_answer(server, &out);
}

static void completion(Server *server, const KestJson *id) {
    KestSaid out = {0};
    start_answer(&out, id);
    kest_wire_say(&out, "{\"isIncomplete\":false,\"items\":[");
    bool first = true;
    static const char *const WORDS[] = {
        "break",  "const", "continue", "defer",   "else",  "enum",
        "extern", "false", "fn",       "for",     "if",    "import",
        "in",     "let",   "match",    "module",  "none",  "return",
        "struct", "true",  "while",    "scratch", "flags", "own",
    };
    for (size_t i = 0; i < sizeof(WORDS) / sizeof(WORDS[0]); i++) {
        kest_wire_say(&out, first ? "" : ",");
        first = false;
        kest_wire_say(&out, "{\"label\":");
        kest_wire_text(&out, WORDS[i]);
        kest_wire_say(&out, ",\"kind\":14}");
    }
    if (server->build != NULL && server->build->program != NULL) {
        KestProgram *program = server->build->program;
        for (uint32_t i = 0; i < program->global_count; i++) {
            const KestSymbol *symbol = &program->globals[i];
            kest_wire_say(&out, first ? "" : ",");
            first = false;
            kest_wire_say(&out, "{\"label\":");
            kest_wire_text(&out, symbol->name);
            kest_wire_sayf(
                &out, ",\"kind\":%d,\"detail\":",
                symbol->type != NULL && symbol->type->tag == KEST_T_FN ? 3
                                                                       : 21);
            kest_wire_text(&out,
                           kest_type_name(server->build->arena, symbol->type));
            kest_wire_char(&out, '}');
        }
    }
    kest_wire_say(&out, "]}");
    finish_answer(server, &out);
}

// The one form, which is the formatter this tree already has: an editor asking
// for a file to be formatted gets exactly what `kest fmt` writes, because it is
// the same call.
static void formatting(Server *server, const KestJson *id) {
    KestSaid out = {0};
    start_answer(&out, id);
    // The formatter hands back the file as it should be written, in arena
    // memory, which is the same call `kest fmt` makes: an editor formatting a
    // file gets exactly what the command line would have written.
    // Only a file that was read whole and without a mistake, as `kest fmt`
    // formats only one that parses, and only into the words it holds: the
    // edit replaces the whole of the file, so a tree that was read short --
    // a file a nought byte ended -- was the rest of the file deleted.
    const char *formatted = NULL;
    size_t formatted_size = 0;
    if (server->build != NULL && server->build->units.count > 0 &&
        server->build->diags.error_count == 0) {
        const KestSource *source = &server->build->units.items[0].source;
        formatted = kest_format(&server->build->units.items[0].unit, source,
                                server->build->arena, &formatted_size);
        KestSource written;
        if (formatted != NULL &&
            (!kest_source_init(&written, server->build->arena, source->path,
                               formatted, formatted_size) ||
             kest_format_same_words(server->build->arena, source, &written) !=
                 KEST_WORDS_SAME)) {
            formatted = NULL;
        }
    }
    if (formatted == NULL) {
        kest_wire_say(&out, "null");
    } else {
        Open *file = here(server);
        kest_wire_say(&out, "[{\"range\":");
        put_range(&out, file->text, file->length, 0, file->length);
        kest_wire_say(&out, ",\"newText\":");
        kest_wire_escaped(&out, formatted, formatted_size);
        kest_wire_say(&out, "}]");
    }
    finish_answer(server, &out);
}

// A file the client has opened, added to the set rather than put in place of
// what was there. Opening one the set already holds replaces its text, which
// is what a client does when it reopens a file it never told this it closed.
static void opened(Server *server, const KestJson *params) {
    const KestJson *uri = kest_wire_down(params, "textDocument", "uri");
    const KestJson *text = kest_wire_down(params, "textDocument", "text");
    if (uri == NULL || uri->kind != KEST_JSON_TEXT) {
        return;
    }
    Open *file;
    if (point_at(server, uri->text, uri->length)) {
        file = here(server);
    } else {
        file = room_for_another(server);
        if (file == NULL) {
            return;
        }
        server->at = server->count - 1;
        file->uri = copy_of(uri->text, uri->length);
        file->path = path_of_uri(uri->text, uri->length);
    }
    free(file->text);
    if (text != NULL && text->kind == KEST_JSON_TEXT) {
        file->text = copy_of(text->text, text->length);
    } else {
        file->text = copy_of("", 0);
    }
    file->length = file->text == NULL ? 0 : strlen(file->text);
    build_again(server);
    publish(server);
}

static void changed(Server *server, const KestJson *params) {
    const KestJson *changes = kest_wire_member(params, "contentChanges");
    if (changes == NULL || changes->kind != KEST_JSON_LIST ||
        changes->count == 0) {
        return;
    }
    // Whole documents, which is what this server asks for: a range change is
    // an edit this would have to apply itself, and applying an edit twice is
    // the one way a language server can be wrong about what the file says.
    const KestJson *text =
        kest_wire_member(changes->items[changes->count - 1], "text");
    if (text == NULL || text->kind != KEST_JSON_TEXT) {
        return;
    }
    Open *file = here(server);
    char *fresh = copy_of(text->text, text->length);
    if (fresh == NULL) {
        return;
    }
    free(file->text);
    file->text = fresh;
    file->length = text->length;
    build_again(server);
    publish(server);
}

// A file the client has closed is one this no longer has the text of, and the
// disk has it again. The build goes with it when it was of that file, because
// what it was built from has gone.
static void closed(Server *server) {
    Open *file = here(server);
    if (file == &nothing_open) {
        return;
    }
    if (server->built != NULL && file->path != NULL &&
        strcmp(server->built, file->path) == 0) {
        kest_build_free(server->build);
        server->build = NULL;
        free(server->built);
        server->built = NULL;
    }
    free(file->uri);
    free(file->path);
    free(file->text);
    *file = server->open[server->count - 1];
    server->count--;
    server->at = server->count;
}

static bool method_starts(const KestJson *method, const char *prefix) {
    size_t length = strlen(prefix);
    return method != NULL && method->kind == KEST_JSON_TEXT &&
           method->length >= length &&
           memcmp(method->text, prefix, length) == 0;
}

// Which file a message is about, out of the set the client has opened. Every
// `textDocument` method carries the uri of the one it means, and a server that
// did not read it answered about whichever file it happened to be holding.
static bool pointed_at(Server *server, const KestJson *params) {
    const KestJson *uri = kest_wire_down(params, "textDocument", "uri");
    if (uri == NULL || uri->kind != KEST_JSON_TEXT) {
        server->at = server->count;
        return false;
    }
    return point_at(server, uri->text, uri->length);
}

static bool method_is(const KestJson *method, const char *name) {
    return method != NULL && method->kind == KEST_JSON_TEXT &&
           kest_word_same(name, method->text, method->length);
}

static void handle(Server *server, const KestJson *message) {
    const KestJson *method = kest_wire_member(message, "method");
    const KestJson *id = kest_wire_member(message, "id");
    const KestJson *params = kest_wire_member(message, "params");
    server->asking = id;

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
    // Every message from here on is about the file its uri names. Choosing
    // once, before the dispatch below, is what makes that true of all of them
    // at once rather than of whichever handlers remembered to ask. A uri
    // nothing was opened for is answered rather than guessed at.
    if (method_starts(method, "textDocument/")) {
        if (!pointed_at(server, params)) {
            if (id != NULL) {
                answer(server, id, "null");
            }
            return;
        }
        // A request is answered out of a build, and the build in hand may be
        // of another file. A notification builds for itself, and building
        // twice for one keystroke is what this stays out of.
        if (id != NULL) {
            ready(server);
        }
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
        closed(server);
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
        const KestJson *query = kest_wire_member(params, "query");
        symbols(
            server, id, false,
            query != NULL && query->kind == KEST_JSON_TEXT ? query->text : "",
            query != NULL && query->kind == KEST_JSON_TEXT ? query->length : 0);
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

int kest_lsp_serve(const char *library, FILE *in, FILE *out,
                   KestKestFiles files, KestResolve resolve) {
    Server server = {0};
    server.files = files;
    server.resolve = resolve;
    server.arena = kest_arena_new();
    if (server.arena == NULL) {
        return 1;
    }
    server.library = library;
    server.out = out;

    while (true) {
        char *body = NULL;
        size_t length = 0;
        KestWireNext next = kest_wire_next(in, &body, &length);
        if (next == KEST_WIRE_ENDED) {
            break;
        }
        if (next == KEST_WIRE_UNHELD) {
            send(&server, NO_ROOM_TO_READ, sizeof NO_ROOM_TO_READ - 1);
            continue;
        }

        // One arena per message, rewound after it: a message is read into a
        // tree and the tree is no use once it is answered.
        KestMark before = kest_arena_mark(server.arena);
        KestJson *message = NULL;
        KestWireRead read =
            kest_wire_read(server.arena, body, length, &message);
        switch (read) {
        case KEST_WIRE_JSON:
            handle(&server, message);
            server.asking = NULL;
            break;
        case KEST_WIRE_NO_ROOM:
            send(&server, NO_ROOM_TO_READ, sizeof NO_ROOM_TO_READ - 1);
            break;
        case KEST_WIRE_NOT_JSON:
            send(&server, NOT_JSON, sizeof NOT_JSON - 1);
            break;
        case KEST_WIRE_TOO_DEEP:
            too_deep(&server);
            break;
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
    free(server.built);
    for (size_t i = 0; i < server.spoken_count; i++) {
        free(server.spoken[i]);
    }
    free(server.spoken);
    for (size_t i = 0; i < server.count; i++) {
        free(server.open[i].uri);
        free(server.open[i].path);
        free(server.open[i].text);
    }
    free(server.open);
    kest_arena_free(server.arena);
    return 0;
}
