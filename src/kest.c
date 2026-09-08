#include "kest.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    KestNative function;
    void *context;
} Binding;

struct KestHost {
    Binding *items;
    uint32_t count;
    uint32_t capacity;
};

const char *kest_version(void) {
    return KEST_VERSION_STRING;
}

KestHost *kest_host_new(void) {
    return calloc(1, sizeof(KestHost));
}

void kest_host_free(KestHost *host) {
    if (host == NULL) {
        return;
    }
    for (uint32_t i = 0; i < host->count; i++) {
        free((void *)host->items[i].name);
    }
    free(host->items);
    free(host);
}

bool kest_host_bind(KestHost *host, const char *name, KestNative function,
                    void *context) {
    // A name is bound once. Binding it again silently replaced what was there
    // and told the caller it had worked, which is only true until a machine
    // has started: a machine takes what the host held when it started and
    // keeps it. Either answer surprises somebody, so neither is given.
    for (uint32_t i = 0; i < host->count; i++) {
        if (strcmp(host->items[i].name, name) == 0) {
            return false;
        }
    }

    if (host->count == host->capacity) {
        uint32_t grown = host->capacity == 0 ? 8 : host->capacity * 2;
        Binding *moved = realloc(host->items, sizeof(Binding) * grown);
        if (moved == NULL) {
            return false;
        }
        host->items = moved;
        host->capacity = grown;
    }

    char *owned = malloc(strlen(name) + 1);
    if (owned == NULL) {
        return false;
    }
    memcpy(owned, name, strlen(name) + 1);
    host->items[host->count].name = owned;
    host->items[host->count].function = function;
    host->items[host->count].context = context;
    host->count++;
    return true;
}

KestNative kest_host_find(const KestHost *host, const char *name,
                          void **context) {
    for (uint32_t i = 0; i < host->count; i++) {
        if (strcmp(host->items[i].name, name) == 0) {
            if (context != NULL) {
                *context = host->items[i].context;
            }
            return host->items[i].function;
        }
    }
    return NULL;
}
