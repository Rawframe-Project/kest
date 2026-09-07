#ifndef KEST_DIAG_H
#define KEST_DIAG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "mem.h"

// A half-open byte range in a source file. A zero length means the diagnostic
// is about the file rather than about a place in it, and it renders without a
// source line or a caret.
typedef struct {
    uint32_t offset;
    uint32_t length;
} KestSpan;

typedef enum {
    KEST_SEVERITY_ERROR,
    KEST_SEVERITY_WARNING,
} KestSeverity;

typedef struct {
    KestSeverity severity;
    const char *code;
    const char *message;
    // The fix, when one is knowable. Rendered beside the caret.
    const char *suggestion;
    KestSpan span;
} KestDiag;

// A source file, with its line offsets precomputed so a byte offset can be
// turned into a line and column without rescanning.
typedef struct {
    const char *path;
    const char *text;
    size_t length;
    uint32_t *line_offsets;
    uint32_t line_count;
} KestSource;

// A collected run of diagnostics. Compilation never stops at the first error,
// so this holds everything one pass found.
typedef struct {
    KestArena *arena;
    KestDiag *items;
    uint32_t count;
    uint32_t capacity;
    uint32_t error_count;
} KestDiags;

bool kest_source_init(KestSource *source, KestArena *arena, const char *path,
                      const char *text, size_t length);

// Line and column are one-based. Column counts characters rather than bytes,
// so a caret lands under the right glyph in a UTF-8 identifier.
void kest_source_locate(const KestSource *source, uint32_t offset,
                        uint32_t *line, uint32_t *column);

void kest_diags_init(KestDiags *diags, KestArena *arena);

// Formats and records a diagnostic. The message is copied into the arena.
void kest_diags_add(KestDiags *diags, KestSeverity severity, const char *code,
                    KestSpan span, const char *format, ...);

// Attaches a fix to the most recent diagnostic. Does nothing when there is
// none, so a caller need not check.
void kest_diags_suggest(KestDiags *diags, const char *format, ...);

// Orders diagnostics by where they are in the file. Stages find problems in
// the order that suits the stage, and a reader scans in the order of the text.
void kest_diags_sort(KestDiags *diags);

// Renders for a person: severity, code, location, the source line, a caret
// under the span, and the suggestion.
void kest_diags_render(const KestDiags *diags, const KestSource *source,
                       FILE *out);

// Renders the identical set as JSON, for tooling and for models repairing
// their own output.
void kest_diags_render_json(const KestDiags *diags, const KestSource *source,
                            FILE *out);

#endif
