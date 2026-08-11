#ifndef FSH_ENV_ALIAS_H
#define FSH_ENV_ALIAS_H

#include <stddef.h>

typedef struct {
    char *name;
    char *value;
} AliasEntry;

void alias_set(const char *name, const char *value);
int alias_remove(const char *name);
const char *alias_get(const char *name);
const AliasEntry *alias_list(size_t *count);
char *alias_expand_line(const char *input);

#endif
