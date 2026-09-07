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

typedef struct KestSource KestSource;

// A second place the diagnostic is about. A duplicate is about two
// declarations and a broken promise is about every call between the promise
// and the body that breaks it, and prose naming a line number is a worse way
// to say either.
typedef struct {
    KestSpan span;
    const KestSource *source;
    const char *label;
} KestNote;

#define KEST_MAX_NOTES 8

typedef struct {
    KestSeverity severity;
    const char *code;
    const char *message;
    // The fix, when one is knowable. Rendered beside the caret.
    const char *suggestion;
    KestSpan span;
    // Which file the span is in. A program is more than one file, so a span
    // on its own does not say where it is.
    const KestSource *source;
    KestNote notes[KEST_MAX_NOTES];
    uint8_t note_count;
} KestDiag;

// A source file, with its line offsets precomputed so a byte offset can be
// turned into a line and column without rescanning.
struct KestSource {
    const char *path;
    const char *text;
    size_t length;
    uint32_t *line_offsets;
    uint32_t line_count;
};

// A collected run of diagnostics. Compilation never stops at the first error,
// so this holds everything one pass found.
typedef struct {
    KestArena *arena;
    KestDiag *items;
    uint32_t count;
    uint32_t capacity;
    uint32_t error_count;
    // Where the spans handed to kest_diags_add are, until it is set again.
    // Every stage works on one file at a time, so this is set once per file
    // rather than passed through every call that might report.
    const KestSource *source;
    bool muted;
} KestDiags;

bool kest_source_init(KestSource *source, KestArena *arena, const char *path,
                      const char *text, size_t length);

// Line and column are one-based. Column counts characters rather than bytes,
// so a caret lands under the right glyph in a UTF-8 identifier.
void kest_source_locate(const KestSource *source, uint32_t offset,
                        uint32_t *line, uint32_t *column);

void kest_diags_init(KestDiags *diags, KestArena *arena);

// Says which file the spans of the diagnostics reported next are in.
void kest_diags_in(KestDiags *diags, const KestSource *source);

// Stops anything being recorded, for a pass whose purpose is to find out
// rather than to report. Reporting the same thing twice is worse than not
// reporting it once.
void kest_diags_mute(KestDiags *diags, bool muted);

// Formats and records a diagnostic. The message is copied into the arena.
void kest_diags_add(KestDiags *diags, KestSeverity severity, const char *code,
                    KestSpan span, const char *format, ...);

// Attaches a fix to the most recent diagnostic. Does nothing when there is
// none, so a caller need not check.
void kest_diags_suggest(KestDiags *diags, const char *format, ...);

// Adds a second place to the most recent diagnostic, in the file given, or in
// the current one when that is NULL. Does nothing when there is no diagnostic
// or no room, so a caller need not check.
void kest_diags_note(KestDiags *diags, const KestSource *source, KestSpan span,
                     const char *format, ...);

// Orders diagnostics by where they are in the file. Stages find problems in
// the order that suits the stage, and a reader scans in the order of the text.
void kest_diags_sort(KestDiags *diags);

// Renders for a person: severity, code, location, the source line, a caret
// under the span, and the suggestion.
void kest_diags_render(const KestDiags *diags, FILE *out);

// Renders the identical set as JSON, for tooling and for models repairing
// their own output.
void kest_diags_render_json(const KestDiags *diags, FILE *out);

// The same without the object around it, for when something else is going in
// beside it.
void kest_diags_write_json(const KestDiags *diags, FILE *out);

#endif
