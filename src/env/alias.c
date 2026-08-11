#include "env/alias.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALIAS_MAX_DEPTH 10

static AliasEntry *g_aliases = NULL;
static size_t g_count = 0;
static size_t g_capacity = 0;

static long find_index(const char *name) {
    for (size_t i = 0; i < g_count; i++) {
        if (strcmp(g_aliases[i].name, name) == 0) {
            return (long)i;
        }
    }
    return -1;
}

void alias_set(const char *name, const char *value) {
    long idx = find_index(name);
    if (idx >= 0) {
        free(g_aliases[idx].value);
        g_aliases[idx].value = strdup(value);
        return;
    }
    if (g_count == g_capacity) {
        g_capacity = g_capacity ? g_capacity * 2 : 8;
        g_aliases = realloc(g_aliases, g_capacity * sizeof(AliasEntry));
    }
    g_aliases[g_count].name = strdup(name);
    g_aliases[g_count].value = strdup(value);
    g_count++;
}

int alias_remove(const char *name) {
    long idx = find_index(name);
    if (idx < 0) {
        return 0;
    }
    free(g_aliases[idx].name);
    free(g_aliases[idx].value);
    for (size_t i = (size_t)idx; i + 1 < g_count; i++) {
        g_aliases[i] = g_aliases[i + 1];
    }
    g_count--;
    return 1;
}

const char *alias_get(const char *name) {
    long idx = find_index(name);
    if (idx < 0) {
        return NULL;
    }
    return g_aliases[idx].value;
}

const AliasEntry *alias_list(size_t *count) {
    *count = g_count;
    return g_aliases;
}

static size_t first_word_len(const char *s) {
    size_t i = 0;
    while (s[i] != '\0' && !isspace((unsigned char)s[i])) {
        i++;
    }
    return i;
}

char *alias_expand_line(const char *input) {
    char *result = strdup(input);
    char *seen[ALIAS_MAX_DEPTH];
    int seen_count = 0;
    int depth = 0;

    while (depth < ALIAS_MAX_DEPTH) {
        const char *trimmed = result;
        while (*trimmed == ' ' || *trimmed == '\t') {
            trimmed++;
        }
        size_t wlen = first_word_len(trimmed);
        if (wlen == 0) {
            break;
        }

        char *first_word = malloc(wlen + 1);
        memcpy(first_word, trimmed, wlen);
        first_word[wlen] = '\0';

        int already_seen = 0;
        for (int i = 0; i < seen_count; i++) {
            if (strcmp(seen[i], first_word) == 0) {
                already_seen = 1;
                break;
            }
        }
        if (already_seen) {
            free(first_word);
            break;
        }

        const char *value = alias_get(first_word);
        if (!value) {
            free(first_word);
            break;
        }

        seen[seen_count++] = first_word;

        size_t new_len = strlen(value) + strlen(trimmed + wlen) + 1;
        char *new_result = malloc(new_len);
        snprintf(new_result, new_len, "%s%s", value, trimmed + wlen);
        free(result);
        result = new_result;
        depth++;
    }

    for (int i = 0; i < seen_count; i++) {
        free(seen[i]);
    }

    return result;
}
