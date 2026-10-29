#ifndef KEST_LSP_H
#define KEST_LSP_H

#include <stdbool.h>
#include <stdio.h>

// A language server, speaking LSP over two streams. It is this compiler and
// not a reader of its own: every answer it gives -- what a name is, where it
// was declared, what else names it, what is wrong with the file -- comes from
// the same check a build does, over the same tree, through the index the
// checker writes as it resolves. A second parser inside an editor is a second
// semantic pipeline, which is the thing this project does not have.
//
// Every Kest file under a directory and the directories under it, each handed
// to `each`: false when the host could not say all of them. A rename of a
// name another file declares edits every file of the project that names it,
// so it has to know every file there is, and listing a directory is a thing
// libc cannot do. The host that runs this server answers it.
// A link to a directory is not part of the project, and the walk does not go
// into one.
typedef void (*KestFileFound)(void *context, const char *path);
typedef bool (*KestKestFiles)(const char *directory, KestFileFound each,
                              void *context);

// Where a path really is, every link followed, written into `into`: false
// when the host cannot say. What a rename may change is decided on these,
// because a path a project spells inside itself may lead out of it, to a
// checkout somebody else's projects share.
typedef bool (*KestResolve)(const char *path, char *into, size_t room);

// `library` is where the standard library is, the same answer the command line
// works out for itself. `files` lists a project's files and `resolve` says
// where a path leads; a server without either refuses a rename it would need
// them for. Returns what the process should exit with.
int kest_lsp_serve(const char *library, FILE *in, FILE *out,
                   KestKestFiles files, KestResolve resolve);

#endif
