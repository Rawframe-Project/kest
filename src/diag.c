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

void kest_diags_add(KestDiags *diags, KestSeverity severity, const char *code,
                    KestSpan span, const char *format, ...) {
    if (!diags_reserve(diags)) {
        return;
    }

    va_list args;
    va_start(args, format);
    char *message = format_into(diags->arena, format, args);
    va_end(args);
    if (message == NULL) {
        return;
    }

    KestDiag *diag = &diags->items[diags->count++];
    diag->severity = severity;
    diag->code = code;
    diag->message = message;
    diag->suggestion = NULL;
    diag->span = span;

    if (severity == KEST_SEVERITY_ERROR) {
        diags->error_count++;
    }
}

void kest_diags_suggest(KestDiags *diags, const char *format, ...) {
    if (diags->count == 0) {
        return;
    }

    va_list args;
    va_start(args, format);
    char *text = format_into(diags->arena, format, args);
    va_end(args);

    diags->items[diags->count - 1].suggestion = text;
}

void kest_diags_sort(KestDiags *diags) {
    // Insertion sort: a run holds tens of diagnostics, and keeping equal
    // offsets in the order they were reported keeps a cause ahead of its
    // consequence.
    for (uint32_t i = 1; i < diags->count; i++) {
        KestDiag moving = diags->items[i];
        uint32_t j = i;
        while (j > 0 && diags->items[j - 1].span.offset > moving.span.offset) {
            diags->items[j] = diags->items[j - 1];
            j--;
        }
        diags->items[j] = moving;
    }
}

static void render_line(const KestSource *source, uint32_t line, FILE *out) {
    uint32_t start = source->line_offsets[line - 1];
    uint32_t end = line < source->line_count ? source->line_offsets[line]
                                             : (uint32_t)source->length;
    while (end > start && (source->text[end - 1] == '\n' ||
                           source->text[end - 1] == '\r')) {
        end--;
    }
    fwrite(source->text + start, 1, end - start, out);
}

void kest_diags_render(const KestDiags *diags, const KestSource *source,
                       FILE *out) {
    for (uint32_t i = 0; i < diags->count; i++) {
        const KestDiag *diag = &diags->items[i];

        fprintf(out, "%s[%s]: %s\n", severity_name(diag->severity), diag->code,
                diag->message);

        if (diag->span.length == 0) {
            fprintf(out, "  --> %s\n", source->path);
            if (diag->suggestion != NULL) {
                fprintf(out, "      %s\n", diag->suggestion);
            }
            fprintf(out, "\n");
            continue;
        }

        uint32_t line = 0;
        uint32_t column = 0;
        kest_source_locate(source, diag->span.offset, &line, &column);

        int gutter = snprintf(NULL, 0, "%u", line);
        fprintf(out, "%*s--> %s:%u:%u\n", gutter, "", source->path, line,
                column);
        fprintf(out, "%*s|\n", gutter + 1, "");
        fprintf(out, "%*u | ", gutter, line);
        render_line(source, line, out);
        fprintf(out, "\n%*s| %*s", gutter + 1, "", (int)column - 1, "");

        uint32_t width = diag->span.length == 0 ? 1 : diag->span.length;
        for (uint32_t caret = 0; caret < width; caret++) {
            fputc('^', out);
        }
        if (diag->suggestion != NULL) {
            fprintf(out, " %s", diag->suggestion);
        }
        fprintf(out, "\n\n");
    }
}

static void write_json_string(const char *text, FILE *out) {
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

void kest_diags_render_json(const KestDiags *diags, const KestSource *source,
                            FILE *out) {
    fputs("{\"diagnostics\":[", out);
    for (uint32_t i = 0; i < diags->count; i++) {
        const KestDiag *diag = &diags->items[i];

        uint32_t line = 0;
        uint32_t column = 0;
        kest_source_locate(source, diag->span.offset, &line, &column);

        if (i > 0) {
            fputc(',', out);
        }
        fprintf(out, "{\"severity\":\"%s\",\"code\":\"%s\",\"file\":",
                severity_name(diag->severity), diag->code);
        write_json_string(source->path, out);
        fprintf(out, ",\"line\":%u,\"column\":%u,\"offset\":%u,\"length\":%u",
                line, column, diag->span.offset, diag->span.length);
        fputs(",\"message\":", out);
        write_json_string(diag->message, out);
        if (diag->suggestion != NULL) {
            fputs(",\"suggestion\":", out);
            write_json_string(diag->suggestion, out);
        }
        fputc('}', out);
    }
    fprintf(out, "],\"errors\":%u}\n", diags->error_count);
}
