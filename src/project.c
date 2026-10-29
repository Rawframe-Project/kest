#include "project.h"

#include <stdio.h>
#include <string.h>

#include "diag.h"
#include "kest.h"

// A line of the file, with the ends taken off: where it starts and how long
// it is. Read where it stands rather than cut out of the file with a nought,
// so the file is left as it was read and is what a release carries; cut up,
// it had to be read off the disk a second time for that.
static bool next_line(const char **at, const char **line, size_t *length) {
    if (**at == '\0') {
        return false;
    }
    const char *start = *at;
    const char *end = strchr(start, '\n');
    if (end == NULL) {
        end = start + strlen(start);
        *at = end;
    } else {
        *at = end + 1;
        // A file written on the other kind of machine ends its lines with two
        // characters, and the first of them is not part of the line.
        if (end > start && end[-1] == '\r') {
            end--;
        }
    }
    *line = start;
    *length = (size_t)(end - start);
    return true;
}

// The word at the front of a line and what follows it, with the space between
// taken off. False for a line that says nothing.
static bool split_line(const char *line, size_t length, const char **name,
                       size_t *name_length, const char **value,
                       size_t *value_length) {
    const char *end = line + length;
    while (line < end && (*line == ' ' || *line == '\t')) {
        line++;
    }
    if (line == end || *line == '#') {
        return false;
    }
    const char *at = line;
    while (at < end && *at != ' ' && *at != '\t') {
        at++;
    }
    *name = line;
    *name_length = (size_t)(at - line);
    while (at < end && (*at == ' ' || *at == '\t')) {
        at++;
    }
    *value = at;
    *value_length = (size_t)(end - at);
    return true;
}

static const char NO_ROOM[] = "there was no room to read the project";

static char *read_whole(KestArena *arena, const char *path, bool *there) {
    *there = false;
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    *there = true;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    // A manifest is a page. Anything longer is something else that happens to
    // be called one, and reading it would be reading whatever was handed over.
    if (size < 0 || size > (1 << 20)) {
        fclose(file);
        return NULL;
    }
    char *text = kest_arena_alloc(arena, (size_t)size + 1, 1);
    if (text == NULL) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(text, 1, (size_t)size, file);
    fclose(file);
    text[read] = '\0';
    return text;
}

void kest_project_path(const char *where, char *path, size_t room) {
    size_t length = strlen(where);
    size_t named = strlen(KEST_PROJECT_FILE);
    if (length >= named &&
        strcmp(where + length - named, KEST_PROJECT_FILE) == 0) {
        snprintf(path, room, "%s", where);
    } else if (length == 0) {
        snprintf(path, room, "%s", KEST_PROJECT_FILE);
    } else {
        snprintf(path, room, "%s%s%s", where,
                 where[length - 1] == '/' ? "" : "/", KEST_PROJECT_FILE);
    }
}

KestProject *kest_project_read(KestArena *arena, const char *where,
                               const char **why) {
    return kest_project_from(arena, where, NULL, why);
}

KestProject *kest_project_from(KestArena *arena, const char *where,
                               const char *handed, const char **why) {
    *why = NULL;
    if (arena == NULL || where == NULL) {
        return NULL;
    }
    char path[1024];
    kest_project_path(where, path, sizeof(path));

    // A manifest handed over is the one there is, and nothing is looked for on
    // a disk.
    bool there = handed != NULL;
    const char *text = handed != NULL
                           ? kest_arena_strndup(arena, handed, strlen(handed))
                           : read_whole(arena, path, &there);
    if (text == NULL) {
        // A file that is not there is not a mistake: a program is a file and
        // needs no project around it. A file that is there and will not be
        // read is.
        *why =
            there ? "there is a project here and it could not be read" : NULL;
        return NULL;
    }

    KestProject *project = KEST_ARENA_NEW(arena, KestProject);
    if (project == NULL) {
        *why = NO_ROOM;
        return NULL;
    }
    memset(project, 0, sizeof(*project));
    project->text = text;
    project->path = kest_arena_strndup(arena, path, strlen(path));
    project->name = "";
    project->entry = "";
    project->needs_kest = "";
    project->profile = "";
    project->edition = KEST_EDITION_STRING;
    project->tests = "";

    const char *at = text;
    const char *line = NULL;
    size_t line_length = 0;
    while (next_line(&at, &line, &line_length)) {
        const char *name = NULL;
        size_t name_length = 0;
        const char *written = NULL;
        size_t written_length = 0;
        if (!split_line(line, line_length, &name, &name_length, &written,
                        &written_length)) {
            continue;
        }
        // Refused rather than read as the nearest one there is: a project
        // written for an edition this compiler has not got is a project whose
        // words may mean something here they did not mean there.
        if (kest_word_same("edition", name, name_length) &&
            !kest_word_same(KEST_EDITION_STRING, written, written_length)) {
            *why = "a project says an edition this does not know";
            return NULL;
        }
        const char *value = kest_arena_strndup(arena, written, written_length);
        if (value == NULL) {
            *why = NO_ROOM;
            return NULL;
        }
        if (kest_word_same("project", name, name_length)) {
            project->name = value;
        } else if (kest_word_same("entry", name, name_length)) {
            project->entry = value;
        } else if (kest_word_same("kest", name, name_length)) {
            project->needs_kest = value;
        } else if (kest_word_same("profile", name, name_length)) {
            project->profile = value;
        } else if (kest_word_same("edition", name, name_length)) {
            project->edition = value;
        } else if (kest_word_same("tests", name, name_length)) {
            project->tests = value;
        } else if (kest_word_same("source", name, name_length)) {
            if (project->source_count >= KEST_MOST_SOURCES) {
                *why = "a project says more places to look than this reads";
                return NULL;
            }
            project->sources[project->source_count++] = value;
        } else {
            *why = "a project says something this does not know";
            return NULL;
        }
    }
    return project;
}

const char *kest_project_written(KestArena *arena, const char *name) {
    char room[1024];
    int written = snprintf(
        room, sizeof(room),
        "# What this project is, in `name value` lines. `kest doctor` reads\n"
        "# it back and says what it found.\n"
        "project %s\n"
        "\n"
        "# What to run, check or build when nothing else is named.\n"
        "entry src/main.kest\n"
        "\n"
        "# Where this project's own modules are. A module's name has to match\n"
        "# where its file is under one of these. A dependency is another of\n"
        "# these lines pointing at wherever it was put: there is no registry.\n"
        "source src\n"
        "\n"
        "# Where the programs that check this project are. `kest test` runs\n"
        "# each of them and reads what it answered.\n"
        "tests tests\n"
        "\n"
        "# What it was written against, which deterministic profile it is\n"
        "# written under, and which edition of the language: a change that\n"
        "# would break a program is made under a new edition, and this line "
        "is\n"
        "# the one it asks.\n"
        "kest %s\n"
        "profile %s %u\n"
        "edition %s\n",
        name, KEST_VERSION_STRING, KEST_PROFILE_NAME,
        (unsigned)KEST_PROFILE_VERSION, KEST_EDITION_STRING);
    if (written < 0 || (size_t)written >= sizeof(room)) {
        return NULL;
    }
    return kest_arena_strndup(arena, room, (size_t)written);
}
