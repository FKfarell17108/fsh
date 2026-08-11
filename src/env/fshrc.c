#include "env/fshrc.h"

#include "env/alias.h"
#include "util/strbuf.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_path[4096] = {0};

const char *fshrc_path(void) {
    if (g_path[0] == '\0') {
        const char *home = getenv("HOME");
        if (!home) {
            home = "~";
        }
        snprintf(g_path, sizeof(g_path), "%s/.fshrc", home);
    }
    return g_path;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return s;
}

static char *strip_quotes(char *s) {
    size_t len = strlen(s);
    if (len >= 2) {
        if ((s[0] == '\'' && s[len - 1] == '\'') || (s[0] == '"' && s[len - 1] == '"')) {
            s[len - 1] = '\0';
            return s + 1;
        }
    }
    return s;
}

static int is_valid_identifier(const char *s) {
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) {
        return 0;
    }
    for (size_t i = 1; s[i] != '\0'; i++) {
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_')) {
            return 0;
        }
    }
    return 1;
}

static char *expand_env_generic(const char *val) {
    StrBuf sb;
    strbuf_init(&sb);

    size_t len = strlen(val);
    size_t i = 0;

    while (i < len) {
        if (val[i] == '$') {
            size_t start;
            size_t j;
            int braced = 0;

            if (val[i + 1] == '{') {
                braced = 1;
                start = i + 2;
                j = start;
                while (j < len && val[j] != '}') {
                    j++;
                }
            } else {
                start = i + 1;
                j = start;
                while (j < len && (isalnum((unsigned char)val[j]) || val[j] == '_')) {
                    j++;
                }
            }

            size_t namelen = j - start;
            if (namelen == 0) {
                strbuf_push_char(&sb, val[i]);
                i++;
                continue;
            }

            char *name = malloc(namelen + 1);
            memcpy(name, val + start, namelen);
            name[namelen] = '\0';

            if (strcmp(name, "PATH") == 0) {
                strbuf_push_str(&sb, "$PATH");
            } else {
                const char *v = getenv(name);
                if (v) {
                    strbuf_push_str(&sb, v);
                }
            }

            free(name);
            i = braced ? (j < len ? j + 1 : j) : j;
            continue;
        }

        strbuf_push_char(&sb, val[i]);
        i++;
    }

    return strbuf_take(&sb);
}

static char *replace_all(const char *haystack, const char *needle, const char *replacement) {
    StrBuf sb;
    strbuf_init(&sb);
    size_t needle_len = strlen(needle);
    const char *p = haystack;

    while (*p) {
        if (strncmp(p, needle, needle_len) == 0) {
            strbuf_push_str(&sb, replacement);
            p += needle_len;
        } else {
            strbuf_push_char(&sb, *p);
            p++;
        }
    }

    return strbuf_take(&sb);
}

static char *dedupe_path(const char *merged) {
    StrBuf sb;
    strbuf_init(&sb);

    size_t seen_cap = 32;
    size_t seen_count = 0;
    char **seen = malloc(seen_cap * sizeof(char *));

    char *copy = strdup(merged);
    char *saveptr = NULL;
    char *token = strtok_r(copy, ":", &saveptr);
    int first = 1;

    while (token) {
        if (token[0] != '\0') {
            int dup = 0;
            for (size_t i = 0; i < seen_count; i++) {
                if (strcmp(seen[i], token) == 0) {
                    dup = 1;
                    break;
                }
            }
            if (!dup) {
                if (seen_count == seen_cap) {
                    seen_cap *= 2;
                    seen = realloc(seen, seen_cap * sizeof(char *));
                }
                seen[seen_count++] = token;
                if (!first) {
                    strbuf_push_char(&sb, ':');
                }
                strbuf_push_str(&sb, token);
                first = 0;
            }
        }
        token = strtok_r(NULL, ":", &saveptr);
    }

    free(seen);
    char *result = strbuf_take(&sb);
    free(copy);
    return result;
}

void fshrc_load(void) {
    FILE *f = fopen(fshrc_path(), "r");
    if (!f) {
        return;
    }

    if (getenv("PATH") == NULL) {
        setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    }

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        char *trimmed = trim(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }

        if (strncmp(trimmed, "alias ", 6) == 0) {
            char *rest = trim(trimmed + 6);
            char *eq = strchr(rest, '=');
            if (!eq) {
                continue;
            }
            *eq = '\0';
            char *name = trim(rest);
            char *value = trim(eq + 1);
            value = strip_quotes(value);
            if (name[0] != '\0') {
                alias_set(name, value);
            }
            continue;
        }

        char *export_line = trimmed;
        if (strncmp(trimmed, "export ", 7) == 0) {
            export_line = trim(trimmed + 7);
        }

        char *eq = strchr(export_line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        char *key = trim(export_line);
        char *val = trim(eq + 1);
        val = strip_quotes(val);

        if (!is_valid_identifier(key)) {
            continue;
        }

        if (strcmp(key, "PATH") == 0) {
            const char *current_path = getenv("PATH");
            if (!current_path) {
                current_path = "";
            }
            char *expanded = expand_env_generic(val);

            char *with_path;
            if (strstr(expanded, "$PATH") == NULL) {
                size_t needed = strlen(expanded) + strlen(":$PATH") + 1;
                with_path = malloc(needed);
                snprintf(with_path, needed, "%s:$PATH", expanded);
            } else {
                with_path = strdup(expanded);
            }
            free(expanded);

            char *merged = replace_all(with_path, "$PATH", current_path);
            free(with_path);

            char *deduped = dedupe_path(merged);
            free(merged);

            setenv("PATH", deduped, 1);
            free(deduped);
        } else {
            char *expanded = expand_env_generic(val);
            setenv(key, expanded, 1);
            free(expanded);
        }
    }

    fclose(f);
}

int fshrc_generate_default(void) {
    FILE *f = fopen(fshrc_path(), "w");
    if (!f) {
        return -1;
    }

    fputs(
        "export PATH=\"$HOME/.cargo/bin:$PATH\"\n"
        "export PATH=\"$HOME/.npm-global/bin:$PATH\"\n"
        "export PATH=\"$HOME/.local/bin:$PATH\"\n"
        "\n"
        "alias ll='ls -la'\n"
        "alias ..='cd ..'\n"
        "alias ...='cd ../..'\n"
        "alias gs='git status'\n"
        "alias ga='git add .'\n"
        "alias gc='git commit -m'\n"
        "alias gp='git push'\n"
        "alias gl='git log --oneline'\n",
        f);

    fclose(f);
    return 0;
}
