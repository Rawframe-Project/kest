#include "diag.h"

#include <stdarg.h>
#include <string.h>

static const char *severity_name(KestSeverity severity) {
    return severity == KEST_SEVERITY_ERROR ? "error" : "warning";
}

static char *format_into(KestArena *arena, const char *format, va_list args) {
    va_list measure;
    va_copy(measure, args);
    int length = vsnprintf(NULL, 0, format, measure);
    va_end(measure);
    if (length < 0) {
        return NULL;
    }
    char *text = kest_arena_alloc(arena, (size_t)length + 1, 1);
    if (text == NULL) {
        return NULL;
    }
    vsnprintf(text, (size_t)length + 1, format, args);
    return text;
}

bool kest_source_init(KestSource *source, KestArena *arena, const char *path,
                      const char *text, size_t length) {
    source->path = path;
    source->text = text;
    source->length = length;

    uint32_t lines = 1;
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\n') {
            lines++;
        }
    }

    source->line_offsets = KEST_ARENA_ARRAY(arena, uint32_t, lines);
    if (source->line_offsets == NULL) {
        return false;
    }
    source->line_count = lines;

    uint32_t line = 0;
    source->line_offsets[line++] = 0;
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\n' && line < lines) {
            source->line_offsets[line++] = (uint32_t)i + 1;
        }
    }
    return true;
}

// Two letters the wrong way round is one mistake and not two. It is the way a
// word is mistyped most often, and a name of four letters is allowed one
// mistake, so counting a swap as two is the difference between a suggestion
// and none: `psuh` was near nothing, and `push` was right there. Which is why
// the row before last is kept as well.
uint32_t kest_word_distance(const char *a, size_t a_len, const char *b,
                            size_t b_len, uint32_t limit) {
    if (a_len > b_len + limit || b_len > a_len + limit) {
        return limit + 1;
    }

    uint32_t before[64] = {0};
    uint32_t previous[64];
    uint32_t current[64];
    if (b_len >= 64) {
        return limit + 1;
    }

    for (size_t j = 0; j <= b_len; j++) {
        previous[j] = (uint32_t)j;
    }
    for (size_t i = 1; i <= a_len; i++) {
        current[0] = (uint32_t)i;
        uint32_t best = current[0];
        for (size_t j = 1; j <= b_len; j++) {
            uint32_t substitute = previous[j - 1] + (a[i - 1] != b[j - 1]);
            uint32_t remove = previous[j] + 1;
            uint32_t insert = current[j - 1] + 1;
            uint32_t least = substitute < remove ? substitute : remove;
            least = least < insert ? least : insert;
            // The two before this one, each standing where the other is.
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] &&
                a[i - 2] == b[j - 1]) {
                uint32_t swapped = before[j - 2] + 1;
                least = least < swapped ? least : swapped;
            }
            current[j] = least;
            if (least < best) {
                best = least;
            }
        }
        if (best > limit) {
            return limit + 1;
        }
        memcpy(before, previous, sizeof(uint32_t) * (b_len + 1));
        memcpy(previous, current, sizeof(uint32_t) * (b_len + 1));
    }
    return previous[b_len];
}

void kest_source_locate(const KestSource *source, uint32_t offset,
                        uint32_t *line, uint32_t *column) {
    uint32_t low = 0;
    uint32_t high = source->line_count - 1;
    while (low < high) {
        uint32_t middle = (low + high + 1) / 2;
        if (source->line_offsets[middle] <= offset) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }

    // Continuation bytes belong to the character their lead byte started, so
    // skipping them counts glyphs rather than bytes.
    uint32_t characters = 0;
    for (uint32_t i = source->line_offsets[low]; i < offset; i++) {
        if ((source->text[i] & 0xc0) != 0x80) {
            characters++;
        }
    }

    *line = low + 1;
    *column = characters + 1;
}

void kest_diags_init(KestDiags *diags, KestArena *arena) {
    diags->arena = arena;
    diags->items = NULL;
    diags->count = 0;
    diags->capacity = 0;
    diags->error_count = 0;
    diags->source = NULL;
    diags->muted = false;
}

void kest_diags_in(KestDiags *diags, const KestSource *source) {
    diags->source = source;
}

void kest_diags_mute(KestDiags *diags, bool muted) {
    diags->muted = muted;
}

static bool diags_reserve(KestDiags *diags) {
    if (diags->count < diags->capacity) {
        return true;
    }
    uint32_t capacity = diags->capacity == 0 ? 16 : diags->capacity * 2;
    KestDiag *items = KEST_ARENA_ARRAY(diags->arena, KestDiag, capacity);
    if (items == NULL) {
        return false;
    }
    if (diags->count > 0) {
        memcpy(items, diags->items, sizeof(KestDiag) * diags->count);
    }
    diags->items = items;
    diags->capacity = capacity;
    return true;
}

// One place a diagnostic is recorded, so the two ways of formatting its
// message meet before anything is written down.
static void add_formatted(KestDiags *diags, KestSeverity severity,
                          const char *code, KestSpan span,
                          const char *message) {
    KestDiag *diag = &diags->items[diags->count++];
    diag->severity = severity;
    diag->code = code;
    diag->message = message;
    diag->suggestion = NULL;
    diag->span = span;
    diag->source = diags->source;
    diag->note_count = 0;
    diag->left_out = 0;

    if (severity == KEST_SEVERITY_ERROR) {
        diags->error_count++;
    }
}

// The message is formatted into the arena and is as long as it is. A caller
// that wrote it into a buffer of its own first would cut a message off in the
// middle of a name, which is what every one of them used to do.
void kest_diags_addv(KestDiags *diags, KestSeverity severity,
                     const char *code, KestSpan span, const char *format,
                     va_list args) {
    if (diags->muted || !diags_reserve(diags)) {
        return;
    }
    char *message = format_into(diags->arena, format, args);
    if (message == NULL) {
        return;
    }
    add_formatted(diags, severity, code, span, message);
}

void kest_diags_add(KestDiags *diags, KestSeverity severity, const char *code,
                    KestSpan span, const char *format, ...) {
    if (diags->muted || !diags_reserve(diags)) {
        return;
    }

    va_list args;
    va_start(args, format);
    char *message = format_into(diags->arena, format, args);
    va_end(args);
    if (message == NULL) {
        return;
    }
    add_formatted(diags, severity, code, span, message);
}

void kest_diags_suggestv(KestDiags *diags, const char *format, va_list args) {
    if (diags->muted || diags->count == 0) {
        return;
    }
    diags->items[diags->count - 1].suggestion =
        format_into(diags->arena, format, args);
}

void kest_diags_suggest(KestDiags *diags, const char *format, ...) {
    if (diags->muted || diags->count == 0) {
        return;
    }

    va_list args;
    va_start(args, format);
    char *text = format_into(diags->arena, format, args);
    va_end(args);

    diags->items[diags->count - 1].suggestion = text;
}

// One place both of these write a note, because a note about the last
// diagnostic and a note about one further back are the same thing said to a
// different item.
static void note_on(KestDiags *diags, KestDiag *diag, const KestSource *source,
                    KestSpan span, const char *format, va_list args) {
    if (diag->note_count == KEST_MAX_NOTES) {
        // Counted rather than dropped. What a caller does about it is the
        // caller's — several of them keep room for a note that says what is
        // under it — and what happens to one that does not is this.
        diag->left_out++;
        return;
    }
    char *label = format_into(diags->arena, format, args);
    if (label == NULL) {
        return;
    }
    KestNote *note = &diag->notes[diag->note_count++];
    note->span = span;
    note->source = source == NULL ? diags->source : source;
    note->label = label;
}

void kest_diags_note(KestDiags *diags, const KestSource *source, KestSpan span,
                     const char *format, ...) {
    if (diags->muted || diags->count == 0) {
        return;
    }
    va_list args;
    va_start(args, format);
    note_on(diags, &diags->items[diags->count - 1], source, span, format, args);
    va_end(args);
}

void kest_diags_note_at(KestDiags *diags, uint32_t which,
                        const KestSource *source, KestSpan span,
                        const char *format, ...) {
    if (diags->muted || which >= diags->count) {
        return;
    }
    va_list args;
    va_start(args, format);
    note_on(diags, &diags->items[which], source, span, format, args);
    va_end(args);
}

void kest_diags_absorb(KestDiags *into, const KestDiags *from) {
    for (uint32_t i = 0; i < from->count; i++) {
        if (!diags_reserve(into)) {
            return;
        }
        into->items[into->count++] = from->items[i];
        if (from->items[i].severity == KEST_SEVERITY_ERROR) {
            into->error_count++;
        }
    }
}

void kest_diags_sort(KestDiags *diags) {
    // Insertion sort: a run holds tens of diagnostics, and keeping equal
    // offsets in the order they were reported keeps a cause ahead of its
    // consequence.
    for (uint32_t i = 1; i < diags->count; i++) {
        KestDiag moving = diags->items[i];
        uint32_t j = i;
        // Within a file, by position. Between files, the order they were read
        // in, which is the order the imports were followed.
        while (j > 0 && diags->items[j - 1].source != NULL &&
               diags->items[j - 1].source == moving.source &&
               diags->items[j - 1].span.offset > moving.span.offset) {
            diags->items[j] = diags->items[j - 1];
            j--;
        }
        diags->items[j] = moving;
    }
}

// How much of a source line is shown. The formatter writes to eighty columns,
// so a line past this came from a file it could not read, or from a machine,
// which can put a whole program on one of them. What the reader is being shown
// is the span, so the line around it is what is kept.
#define SHOWN_COLUMNS 100
#define LEADING_COLUMNS 20
#define CUT_MARK "..."

typedef struct {
    uint32_t start;
    uint32_t end;
    bool cut_before;
    bool cut_after;
} Shown;

static Shown shown_part(const KestSource *source, uint32_t line, KestSpan span) {
    uint32_t start = source->line_offsets[line - 1];
    uint32_t end = line < source->line_count ? source->line_offsets[line]
                                             : (uint32_t)source->length;
    while (end > start && (source->text[end - 1] == '\n' ||
                           source->text[end - 1] == '\r')) {
        end--;
    }

    Shown shown = {start, end, false, false};
    if (end - start <= SHOWN_COLUMNS) {
        return shown;
    }

    uint32_t at = span.offset < start ? start : span.offset;
    if (at > end) {
        at = end;
    }
    uint32_t from = at - start > LEADING_COLUMNS ? at - LEADING_COLUMNS : start;
    if (from + SHOWN_COLUMNS > end) {
        from = end - SHOWN_COLUMNS;
    }
    shown.start = from;
    shown.end = from + SHOWN_COLUMNS;
    shown.cut_before = from > start;
    shown.cut_after = shown.end < end;
    return shown;
}

// A tab is shown as the spaces it stands for. A caret cannot be put under a
// tab: the caret line would have to guess what the terminal does with one, and
// be wrong wherever it guessed differently. Four is what this language is
// written with, and the guess is only about how wide the line looks, not about
// where the caret lands, because both lines are built the same way.
#define TAB_COLUMNS 4

// The column reached after showing the bytes between `from` and `to`, having
// written them. With nowhere to write to, it measures and writes nothing.
static uint32_t put_expanded(const KestSource *source, uint32_t from,
                             uint32_t to, uint32_t column, FILE *out) {
    for (uint32_t i = from; i < to; i++) {
        if (source->text[i] != '\t') {
            if (out != NULL) {
                fputc(source->text[i], out);
            }
            // A continuation byte is the rest of the character before it and
            // is shown where that one is, so it is not a column of its own.
            // This is what `kest_source_locate` counts, which is why the
            // number beside the path and the caret under the line agree.
            if ((source->text[i] & 0xc0) != 0x80) {
                column++;
            }
            continue;
        }
        uint32_t width = TAB_COLUMNS - column % TAB_COLUMNS;
        for (uint32_t n = 0; n < width && out != NULL; n++) {
            fputc(' ', out);
        }
        column += width;
    }
    return column;
}

static int line_width(const KestSource *source, KestSpan span) {
    uint32_t line = 0;
    uint32_t column = 0;
    kest_source_locate(source, span.offset, &line, &column);
    return snprintf(NULL, 0, "%u", line);
}

// The location, the source line and a caret under the span, with whatever is
// being said about it beside the caret.
static void render_frame(const KestSource *source, KestSpan span,
                         const char *label, int gutter, FILE *out) {
    uint32_t line = 0;
    uint32_t column = 0;
    kest_source_locate(source, span.offset, &line, &column);

    fprintf(out, "%*s--> %s:%u:%u\n", gutter, "", source->path, line, column);
    fprintf(out, "%*s|\n", gutter + 1, "");
    Shown shown = shown_part(source, line, span);
    fprintf(out, "%*u | ", gutter, line);
    uint32_t indent = shown.cut_before ? (uint32_t)strlen(CUT_MARK) : 0;
    if (shown.cut_before) {
        fputs(CUT_MARK, out);
    }
    put_expanded(source, shown.start, shown.end, indent, out);
    if (shown.cut_after) {
        fputs(CUT_MARK, out);
    }

    uint32_t at = span.offset < shown.start ? shown.start : span.offset;
    indent = put_expanded(source, shown.start, at, indent, NULL);
    fprintf(out, "\n%*s| %*s", gutter + 1, "", (int)indent, "");

    uint32_t width = span.length == 0 ? 1 : span.length;
    // A span that runs off the end of a line carets to where it ends, which is
    // how a span over more than one line has always been shown. A span that
    // runs off the end of what is shown stops at the cut, because the mark
    // after the line already says there is more.
    if (shown.cut_after && at + width > shown.end) {
        width = shown.end - at;
    }
    // A span with a tab in it is as wide as the tab was shown, so the carets
    // end where the span does. An empty span has no bytes to measure and is
    // one caret wherever it is.
    if (span.length != 0) {
        width = put_expanded(source, at, at + width, indent, NULL) - indent;
    }
    for (uint32_t caret = 0; caret < width; caret++) {
        fputc('^', out);
    }
    if (label != NULL) {
        fprintf(out, " %s", label);
    }
    fputc('\n', out);
}

void kest_diags_render(const KestDiags *diags, FILE *out) {
    for (uint32_t i = 0; i < diags->count; i++) {
        const KestDiag *diag = &diags->items[i];
        const KestSource *source = diag->source;

        fprintf(out, "%s[%s]: %s\n", severity_name(diag->severity), diag->code,
                diag->message);

        // One gutter for every frame of one diagnostic, so the source lines
        // line up with each other rather than each with itself.
        bool framed = source != NULL && diag->span.length != 0;
        int gutter = framed ? line_width(source, diag->span) : 0;
        for (uint8_t n = 0; n < diag->note_count; n++) {
            if (diag->notes[n].source == NULL) {
                continue;
            }
            int width = line_width(diag->notes[n].source, diag->notes[n].span);
            if (width > gutter) {
                gutter = width;
            }
        }

        if (framed) {
            render_frame(source, diag->span, diag->suggestion, gutter, out);
        } else {
            // A diagnostic about the program rather than about a file has
            // nowhere of its own to point at, and inventing somewhere would be
            // worse. Its notes have their own places and are the whole of what
            // it has to show.
            if (source != NULL) {
                fprintf(out, "  --> %s\n", source->path);
            }
            if (diag->suggestion != NULL) {
                fprintf(out, "      %s\n", diag->suggestion);
            }
        }
        for (uint8_t n = 0; n < diag->note_count; n++) {
            if (diag->notes[n].source == NULL) {
                continue;
            }
            render_frame(diag->notes[n].source, diag->notes[n].span,
                         diag->notes[n].label, gutter, out);
        }
        if (diag->left_out > 0) {
            fprintf(out, "%*sand %u more place%s\n", gutter + 1, "",
                    diag->left_out, diag->left_out == 1 ? "" : "s");
        }
        fputc('\n', out);
    }
}

void kest_json_text(const char *text, FILE *out) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        switch (*p) {
        case '"':
            fputs("\\\"", out);
            break;
        case '\\':
            fputs("\\\\", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        default:
            if (*p < 0x20) {
                fprintf(out, "\\u%04x", *p);
            } else {
                fputc(*p, out);
            }
        }
    }
    fputc('"', out);
}

void kest_diags_render_json(const KestDiags *diags, FILE *out) {
    fputc('{', out);
    kest_diags_write_json(diags, out);
    fputs("}\n", out);
}

void kest_diags_write_json(const KestDiags *diags, FILE *out) {
    fputs("\"diagnostics\":[", out);
    for (uint32_t i = 0; i < diags->count; i++) {
        const KestDiag *diag = &diags->items[i];
        const KestSource *source = diag->source;

        if (i > 0) {
            fputc(',', out);
        }
        fprintf(out, "{\"severity\":\"%s\",\"code\":\"%s\"",
                severity_name(diag->severity), diag->code);
        if (source != NULL) {
            fputs(",\"file\":", out);
            kest_json_text(source->path, out);
            // A span with nothing in it is a diagnostic about the whole file,
            // which the words show as a path and no line. Saying `1:1` here
            // would be a place nobody chose, and a tool would draw it.
            if (diag->span.length > 0) {
                uint32_t line = 0;
                uint32_t column = 0;
                kest_source_locate(source, diag->span.offset, &line, &column);
                fprintf(out,
                        ",\"line\":%u,\"column\":%u,\"offset\":%u,"
                        "\"length\":%u",
                        line, column, diag->span.offset, diag->span.length);
            }
        }
        fputs(",\"message\":", out);
        kest_json_text(diag->message, out);
        if (diag->suggestion != NULL) {
            fputs(",\"suggestion\":", out);
            kest_json_text(diag->suggestion, out);
        }
        if (diag->note_count > 0) {
            fputs(",\"notes\":[", out);
            for (uint8_t n = 0; n < diag->note_count; n++) {
                const KestNote *note = &diag->notes[n];
                fprintf(out, "%s{", n > 0 ? "," : "");
                if (note->source != NULL) {
                    uint32_t note_line = 0;
                    uint32_t note_column = 0;
                    kest_source_locate(note->source, note->span.offset,
                                       &note_line, &note_column);
                    fputs("\"file\":", out);
                    kest_json_text(note->source->path, out);
                    fprintf(out, ",\"line\":%u,\"column\":%u,", note_line,
                            note_column);
                }
                fputs("\"message\":", out);
                kest_json_text(note->label, out);
                fputc('}', out);
            }
            fputc(']', out);
        }
        if (diag->left_out > 0) {
            fprintf(out, ",\"leftOut\":%u", diag->left_out);
        }
        fputc('}', out);
    }
    fprintf(out, "],\"errors\":%u", diags->error_count);
}
