#include "completion/completion.h"

#include "env/alias.h"
#include "util/strbuf.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const COMPLETION_BUILTINS[] = {
    "exit", "echo", "type", "pwd", "cd", "ls", "dir",
    "alias", "unalias", "clear", "history", "trash", "fsh", "fshrc", "neofetch",
    "bookmarks", "search", "helps", "source",
};
#define COMPLETION_BUILTINS_COUNT (sizeof(COMPLETION_BUILTINS) / sizeof(COMPLETION_BUILTINS[0]))

char **completion_tokenize_line(const char *line, size_t *count) {
    char **tokens = NULL;
    size_t cap = 0;
    size_t n = 0;
    StrBuf cur;
    strbuf_init(&cur);
    int in_double = 0;
    int in_single = 0;

    for (const char *p = line; *p; p++) {
        char ch = *p;
        if (ch == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }
        if (ch == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }
        if (ch == ' ' && !in_double && !in_single) {
            if (cur.length > 0) {
                if (n == cap) {
                    cap = cap ? cap * 2 : 8;
                    tokens = realloc(tokens, cap * sizeof(char *));
                }
                tokens[n++] = strbuf_take(&cur);
                strbuf_init(&cur);
            }
            continue;
        }
        strbuf_push_char(&cur, ch);
    }
    if (cur.length > 0) {
        if (n == cap) {
            cap = cap ? cap * 2 : 8;
            tokens = realloc(tokens, cap * sizeof(char *));
        }
        tokens[n++] = strbuf_take(&cur);
    } else {
        strbuf_free(&cur);
    }

    *count = n;
    return tokens;
}

void completion_free_tokens(char **tokens, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(tokens[i]);
    }
    free(tokens);
}

static void list_push(CandidateList *list, const char *s) {
    list->items = realloc(list->items, (list->count + 1) * sizeof(char *));
    list->items[list->count++] = strdup(s);
}

static int list_contains(const CandidateList *list, const char *s) {
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->items[i], s) == 0) {
            return 1;
        }
    }
    return 0;
}

static int str_cmp_qsort(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static CandidateList get_command_candidates(const char *partial) {
    CandidateList list = {0};
    size_t plen = strlen(partial);

    for (size_t i = 0; i < COMPLETION_BUILTINS_COUNT; i++) {
        if (strncmp(COMPLETION_BUILTINS[i], partial, plen) == 0) {
            list_push(&list, COMPLETION_BUILTINS[i]);
        }
    }

    size_t alias_count;
    const AliasEntry *aliases = alias_list(&alias_count);
    for (size_t i = 0; i < alias_count; i++) {
        if (strncmp(aliases[i].name, partial, plen) == 0 && !list_contains(&list, aliases[i].name)) {
            list_push(&list, aliases[i].name);
        }
    }

    const char *path_env = getenv("PATH");
    if (path_env) {
        char *copy = strdup(path_env);
        char *saveptr = NULL;
        char *dir = strtok_r(copy, ":", &saveptr);
        while (dir) {
            DIR *dp = opendir(dir);
            if (dp) {
                struct dirent *entry;
                while ((entry = readdir(dp)) != NULL) {
                    if (strncmp(entry->d_name, partial, plen) != 0 || list_contains(&list, entry->d_name)) {
                        continue;
                    }
                    char full[4096];
                    snprintf(full, sizeof(full), "%s/%s", dir, entry->d_name);
                    if (access(full, X_OK) == 0) {
                        list_push(&list, entry->d_name);
                    }
                }
                closedir(dp);
            }
            dir = strtok_r(NULL, ":", &saveptr);
        }
        free(copy);
    }

    if (list.count > 0) {
        qsort(list.items, list.count, sizeof(char *), str_cmp_qsort);
    }
    return list;
}

typedef struct {
    char dir[4096];
    char prefix[1024];
} FileTarget;

static void resolve_file_target(const char *partial, FileTarget *out) {
    if (partial[0] == '\0' || strcmp(partial, ".") == 0) {
        if (!getcwd(out->dir, sizeof(out->dir))) {
            snprintf(out->dir, sizeof(out->dir), ".");
        }
        out->prefix[0] = '\0';
        return;
    }

    if (strncmp(partial, "~/", 2) == 0) {
        const char *home = getenv("HOME");
        if (!home) {
            home = "";
        }
        const char *rest = partial + 2;
        const char *ls = strrchr(rest, '/');
        if (!ls) {
            snprintf(out->dir, sizeof(out->dir), "%s", home);
            snprintf(out->prefix, sizeof(out->prefix), "%s", rest);
        } else {
            size_t dirlen = (size_t)(ls - rest);
            char sub[4096];
            snprintf(sub, sizeof(sub), "%.*s", (int)dirlen, rest);
            snprintf(out->dir, sizeof(out->dir), "%s/%s", home, sub);
            snprintf(out->prefix, sizeof(out->prefix), "%s", ls + 1);
        }
        return;
    }

    if (strchr(partial, '/')) {
        const char *ls = strrchr(partial, '/');
        char dirpart[4096];
        size_t dirlen = (size_t)(ls - partial);
        if (dirlen == 0) {
            snprintf(dirpart, sizeof(dirpart), "/");
        } else {
            snprintf(dirpart, sizeof(dirpart), "%.*s", (int)dirlen, partial);
        }
        if (dirpart[0] == '/') {
            snprintf(out->dir, sizeof(out->dir), "%s", dirpart);
        } else {
            char cwd[4096];
            if (!getcwd(cwd, sizeof(cwd))) {
                snprintf(cwd, sizeof(cwd), ".");
            }
            snprintf(out->dir, sizeof(out->dir), "%s/%s", cwd, dirpart);
        }
        snprintf(out->prefix, sizeof(out->prefix), "%s", ls + 1);
        return;
    }

    if (!getcwd(out->dir, sizeof(out->dir))) {
        snprintf(out->dir, sizeof(out->dir), ".");
    }
    snprintf(out->prefix, sizeof(out->prefix), "%s", partial);
}

typedef struct {
    char name[1024];
    int is_dir;
} FileCandidate;

static int file_cmp(const void *ap, const void *bp) {
    const FileCandidate *a = ap;
    const FileCandidate *b = bp;
    int cmp = (b->is_dir ? 1 : 0) - (a->is_dir ? 1 : 0);
    if (cmp != 0) {
        return cmp;
    }
    return strcmp(a->name, b->name);
}

static CandidateList get_file_candidates(const char *partial) {
    CandidateList list = {0};
    FileTarget target;
    resolve_file_target(partial, &target);

    DIR *dp = opendir(target.dir);
    if (!dp) {
        return list;
    }

    FileCandidate *entries = NULL;
    size_t count = 0;
    size_t capacity = 0;
    size_t plen = strlen(target.prefix);

    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (strncmp(entry->d_name, target.prefix, plen) != 0) {
            continue;
        }
        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", target.dir, entry->d_name);
        struct stat st;
        int is_dir = (stat(full, &st) == 0) && S_ISDIR(st.st_mode);

        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 32;
            entries = realloc(entries, capacity * sizeof(FileCandidate));
        }
        snprintf(entries[count].name, sizeof(entries[count].name), "%s", entry->d_name);
        entries[count].is_dir = is_dir;
        count++;
    }
    closedir(dp);

    qsort(entries, count, sizeof(FileCandidate), file_cmp);

    for (size_t i = 0; i < count; i++) {
        char display[2048];
        char name_slash[1200];
        snprintf(name_slash, sizeof(name_slash), "%s%s", entries[i].name, entries[i].is_dir ? "/" : "");

        if (strncmp(partial, "~/", 2) == 0) {
            const char *rest = partial + 2;
            const char *ls = strrchr(rest, '/');
            if (!ls) {
                snprintf(display, sizeof(display), "~/%s", name_slash);
            } else {
                size_t dirlen = (size_t)(ls - rest) + 1;
                snprintf(display, sizeof(display), "~/%.*s%s", (int)dirlen, rest, name_slash);
            }
        } else if (strchr(partial, '/')) {
            const char *ls = strrchr(partial, '/');
            size_t dirlen = (size_t)(ls - partial) + 1;
            snprintf(display, sizeof(display), "%.*s%s", (int)dirlen, partial, name_slash);
        } else {
            snprintf(display, sizeof(display), "%s", name_slash);
        }

        list_push(&list, display);
    }

    free(entries);
    return list;
}

CompletionResult completion_get_candidates(const char *line) {
    CompletionResult result;
    memset(&result, 0, sizeof(result));

    size_t token_count;
    char **tokens = completion_tokenize_line(line, &token_count);

    size_t line_len = strlen(line);
    int ends_with_space = line_len > 0 && line[line_len - 1] == ' ';
    int is_first_word = token_count == 0 || (token_count == 1 && !ends_with_space);

    if (is_first_word) {
        snprintf(result.partial, sizeof(result.partial), "%s", token_count > 0 ? tokens[0] : "");
        result.candidates = get_command_candidates(result.partial);
        completion_free_tokens(tokens, token_count);
        return result;
    }

    snprintf(result.partial, sizeof(result.partial), "%s", ends_with_space ? "" : tokens[token_count - 1]);

    if (token_count > 0 && strcmp(tokens[0], "fshrc") == 0) {
        static const char *subs[] = {"init", "reload", "path", "version"};
        for (size_t i = 0; i < 4; i++) {
            if (strncmp(subs[i], result.partial, strlen(result.partial)) == 0) {
                list_push(&result.candidates, subs[i]);
            }
        }
        completion_free_tokens(tokens, token_count);
        return result;
    }
    if (token_count > 0 && strcmp(tokens[0], "neofetch") == 0) {
        static const char *subs[] = {"on", "off", "preview"};
        for (size_t i = 0; i < 3; i++) {
            if (strncmp(subs[i], result.partial, strlen(result.partial)) == 0) {
                list_push(&result.candidates, subs[i]);
            }
        }
        completion_free_tokens(tokens, token_count);
        return result;
    }
    if (token_count > 0 && strcmp(tokens[0], "fsh") == 0) {
        completion_free_tokens(tokens, token_count);
        return result;
    }
    if (token_count > 0 && strcmp(tokens[0], "source") == 0) {
        if (strncmp("~/.fshrc", result.partial, strlen(result.partial)) == 0) {
            list_push(&result.candidates, "~/.fshrc");
        }
        completion_free_tokens(tokens, token_count);
        return result;
    }

    completion_free_tokens(tokens, token_count);
    result.candidates = get_file_candidates(result.partial);
    return result;
}

void completion_free(CandidateList *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

char *completion_common_prefix(const CandidateList *list) {
    if (list->count == 0) {
        return strdup("");
    }
    char *prefix = strdup(list->items[0]);
    for (size_t i = 1; i < list->count; i++) {
        while (prefix[0] != '\0' && strncmp(list->items[i], prefix, strlen(prefix)) != 0) {
            prefix[strlen(prefix) - 1] = '\0';
        }
        if (prefix[0] == '\0') {
            break;
        }
    }
    return prefix;
}
