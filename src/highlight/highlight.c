#include "highlight/highlight.h"

#include "builtins/builtin.h"
#include "env/alias.h"
#include "util/strbuf.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const char *const BUILTINS[] = {
    "exit", "echo", "type", "pwd", "cd", "ls", "dir", "alias", "unalias",
    "clear", "cls", "history", "trash", "fshrc", "neofetch",
    "bookmarks", "search", "helps", "source"};

static const char *const CMD_EDITORS[] = {
    "vim", "vi", "nvim", "nano", "emacs", "micro", "hx", "helix",
    "code", "gedit", "kate", "subl", "atom"};

static const char *const CMD_GIT[] = {"git", "gh", "hub"};

static const char *const CMD_NODE[] = {
    "node", "npm", "npx", "yarn", "pnpm", "bun", "deno", "ts-node"};

static const char *const CMD_PYTHON[] = {
    "python", "python3", "python2", "pip", "pip3", "pipenv", "poetry", "uv"};

static const char *const CMD_SYSTEM[] = {
    "sudo", "su", "systemctl", "service", "journalctl",
    "apt", "apt-get", "dpkg", "snap",
    "pacman", "yay", "brew",
    "kill", "killall", "pkill", "top", "htop", "btop",
    "ps", "pgrep", "lsof", "df", "du", "free", "uname"};

static const char *const CMD_NETWORK[] = {
    "curl", "wget", "ssh", "scp", "sftp", "rsync",
    "ping", "traceroute", "netstat", "ss", "ip", "ifconfig",
    "nmap", "dig", "nslookup", "host"};

static const char *const CMD_FILE_OPS[] = {
    "mkdir", "rmdir", "rm", "cp", "mv", "touch", "ln",
    "chmod", "chown", "chgrp", "find", "locate",
    "tar", "zip", "unzip", "gzip", "gunzip", "7z",
    "cat", "less", "more", "head", "tail", "tee",
    "grep", "awk", "sed", "sort", "uniq", "wc", "cut",
    "diff", "patch", "xargs"};

static const char *const CMD_DOCKER[] = {
    "docker", "docker-compose", "podman", "kubectl", "helm", "k3s"};

static const char *const CMD_BUILD[] = {
    "make", "cmake", "gcc", "g++", "clang", "rustc", "cargo",
    "go", "javac", "java", "mvn", "gradle",
    "tsc", "webpack", "vite", "rollup", "esbuild"};

static const char *const CMD_SHELL[] = {
    "bash", "zsh", "fish", "sh", "dash",
    "source", "export", "env", "printenv", "set", "unset",
    "which", "whereis", "man", "tldr", "info",
    "date", "time", "watch", "sleep"};

static const char *const GIT_SUBCOMMANDS[] = {
    "add", "commit", "push", "pull", "fetch", "merge", "rebase",
    "checkout", "switch", "branch", "status", "log", "diff",
    "stash", "tag", "remote", "clone", "init", "reset", "restore",
    "cherry-pick", "bisect", "blame", "show", "reflog"};

static const char *const NPM_SUBCOMMANDS[] = {
    "install", "uninstall", "update", "run", "start", "build",
    "test", "publish", "init", "ci", "audit", "outdated",
    "link", "pack", "version", "exec", "create"};

static const char *const DOCKER_SUBCOMMANDS[] = {
    "run", "build", "pull", "push", "ps", "images", "exec",
    "stop", "start", "restart", "rm", "rmi", "logs", "inspect",
    "compose", "network", "volume", "system", "container"};

static const char *const SUDO_LIKE[] = {"sudo", "su", "doas", "run0"};

static const char *const LONG_FLAGS_WITH_VALUES[] = {
    "--output", "--file", "--config", "--format", "--target",
    "--host", "--port", "--user", "--password", "--key",
    "--message", "--branch", "--tag", "--name", "--type"};

static int str_in_set(const char *s, const char *const *set, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (strcmp(s, set[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static const char *hl_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void append_color(StrBuf *sb, const char *hex, int dim, const char *text) {
    unsigned int r = 0;
    unsigned int g = 0;
    unsigned int b = 0;
    sscanf(hex, "#%02x%02x%02x", &r, &g, &b);
    char code[32];
    snprintf(code, sizeof(code), "\x1b[38;2;%u;%u;%um", r, g, b);
    strbuf_push_str(sb, code);
    if (dim) {
        strbuf_push_str(sb, "\x1b[2m");
    }
    strbuf_push_str(sb, text);
    strbuf_push_str(sb, "\x1b[0m");
}

static void cmd_color(StrBuf *sb, const char *cmd) {
    const char *base = hl_basename(cmd);
    if (str_in_set(base, BUILTINS, COUNT(BUILTINS))) { append_color(sb, "#73D997", 0, cmd); return; }
    if (alias_get(base)) { append_color(sb, "#A8E6A3", 0, cmd); return; }
    if (str_in_set(base, CMD_EDITORS, COUNT(CMD_EDITORS))) { append_color(sb, "#D4A9F5", 0, cmd); return; }
    if (str_in_set(base, CMD_GIT, COUNT(CMD_GIT))) { append_color(sb, "#FFA878", 0, cmd); return; }
    if (str_in_set(base, CMD_NODE, COUNT(CMD_NODE))) { append_color(sb, "#6EC6BF", 0, cmd); return; }
    if (str_in_set(base, CMD_PYTHON, COUNT(CMD_PYTHON))) { append_color(sb, "#FFD580", 0, cmd); return; }
    if (str_in_set(base, CMD_SYSTEM, COUNT(CMD_SYSTEM))) { append_color(sb, "#FF7B8A", 0, cmd); return; }
    if (str_in_set(base, CMD_NETWORK, COUNT(CMD_NETWORK))) { append_color(sb, "#70D4FF", 0, cmd); return; }
    if (str_in_set(base, CMD_FILE_OPS, COUNT(CMD_FILE_OPS))) { append_color(sb, "#AEDD87", 0, cmd); return; }
    if (str_in_set(base, CMD_DOCKER, COUNT(CMD_DOCKER))) { append_color(sb, "#5BC8F5", 0, cmd); return; }
    if (str_in_set(base, CMD_BUILD, COUNT(CMD_BUILD))) { append_color(sb, "#F5C542", 0, cmd); return; }
    if (str_in_set(base, CMD_SHELL, COUNT(CMD_SHELL))) { append_color(sb, "#B0B8D8", 0, cmd); return; }
    append_color(sb, "#73D997", 0, cmd);
}

static void subcommand_color(StrBuf *sb, const char *parent, const char *sub) {
    const char *base = hl_basename(parent);

    if (str_in_set(base, CMD_GIT, COUNT(CMD_GIT)) && str_in_set(sub, GIT_SUBCOMMANDS, COUNT(GIT_SUBCOMMANDS))) {
        static const char *const destructive[] = {"reset", "rm", "clean", "rebase", "force"};
        static const char *const creative[] = {"init", "clone", "add", "commit", "push", "tag"};
        if (str_in_set(sub, destructive, COUNT(destructive))) { append_color(sb, "#FF7B8A", 0, sub); return; }
        if (str_in_set(sub, creative, COUNT(creative))) { append_color(sb, "#AEDD87", 0, sub); return; }
        append_color(sb, "#FFD580", 0, sub);
        return;
    }

    if ((str_in_set(base, CMD_NODE, COUNT(CMD_NODE)) || strcmp(base, "npx") == 0) &&
        str_in_set(sub, NPM_SUBCOMMANDS, COUNT(NPM_SUBCOMMANDS))) {
        if (strcmp(sub, "install") == 0 || strcmp(sub, "ci") == 0) { append_color(sb, "#6EC6BF", 0, sub); return; }
        if (strcmp(sub, "uninstall") == 0 || strcmp(sub, "rm") == 0) { append_color(sb, "#FF7B8A", 0, sub); return; }
        if (strcmp(sub, "run") == 0 || strcmp(sub, "start") == 0) { append_color(sb, "#AEDD87", 0, sub); return; }
        append_color(sb, "#FFD580", 0, sub);
        return;
    }

    if (str_in_set(base, CMD_DOCKER, COUNT(CMD_DOCKER)) && str_in_set(sub, DOCKER_SUBCOMMANDS, COUNT(DOCKER_SUBCOMMANDS))) {
        if (strcmp(sub, "rm") == 0 || strcmp(sub, "rmi") == 0 || strcmp(sub, "stop") == 0) { append_color(sb, "#FF7B8A", 0, sub); return; }
        if (strcmp(sub, "run") == 0 || strcmp(sub, "build") == 0 || strcmp(sub, "start") == 0) { append_color(sb, "#AEDD87", 0, sub); return; }
        append_color(sb, "#5BC8F5", 0, sub);
        return;
    }

    if (str_in_set(base, SUDO_LIKE, COUNT(SUDO_LIKE))) {
        append_color(sb, "#FF7B8A", 0, sub);
        return;
    }

    append_color(sb, "#FFD580", 0, sub);
}

static void flag_color(StrBuf *sb, const char *flag) {
    if (strncmp(flag, "--", 2) == 0) {
        if (strstr(flag, "force") || strstr(flag, "hard") || strstr(flag, "delete") ||
            strstr(flag, "remove") || strstr(flag, "purge") || strstr(flag, "nuke")) {
            append_color(sb, "#FF7B8A", 0, flag);
            return;
        }
        if (strstr(flag, "help") || strstr(flag, "version") || strstr(flag, "verbose") || strstr(flag, "dry-run")) {
            append_color(sb, "#70D4FF", 0, flag);
            return;
        }
        if (strstr(flag, "output") || strstr(flag, "format") || strstr(flag, "config") || strstr(flag, "file")) {
            append_color(sb, "#FFB347", 0, flag);
            return;
        }
        append_color(sb, "#C9A0F0", 0, flag);
        return;
    }
    if (flag[0] == '-') {
        if (strchr(flag, 'f') || (strchr(flag, 'r') && strlen(flag) == 3)) {
            append_color(sb, "#FF7B8A", 0, flag);
            return;
        }
        if (strchr(flag, 'v') || strchr(flag, 'h')) {
            append_color(sb, "#70D4FF", 0, flag);
            return;
        }
        append_color(sb, "#FFD580", 0, flag);
        return;
    }
    append_color(sb, "#FFD580", 0, flag);
}

typedef enum { FS_DIR, FS_DIR_HIDDEN, FS_FILE, FS_FILE_HIDDEN, FS_NONE } FsKind;

static FsKind resolve_fs_kind(const char *word, int *hidden_out) {
    char resolved[4096];
    const char *home = getenv("HOME");

    if (strncmp(word, "~/", 2) == 0 && home) {
        snprintf(resolved, sizeof(resolved), "%s/%s", home, word + 2);
    } else if (word[0] != '/') {
        char cwd[4096];
        if (!getcwd(cwd, sizeof(cwd))) {
            cwd[0] = '\0';
        }
        snprintf(resolved, sizeof(resolved), "%s/%s", cwd, word);
    } else {
        snprintf(resolved, sizeof(resolved), "%s", word);
    }

    struct stat st;
    if (stat(resolved, &st) != 0) {
        *hidden_out = 0;
        return FS_NONE;
    }

    const char *base = hl_basename(resolved);
    int hidden = base[0] == '.';
    *hidden_out = hidden;

    if (S_ISDIR(st.st_mode)) {
        return hidden ? FS_DIR_HIDDEN : FS_DIR;
    }
    return hidden ? FS_FILE_HIDDEN : FS_FILE;
}

static void path_arg_color(StrBuf *sb, const char *full, FsKind kind) {
    switch (kind) {
        case FS_DIR: append_color(sb, "#6BBFFF", 0, full); break;
        case FS_DIR_HIDDEN: append_color(sb, "#4A90B8", 0, full); break;
        case FS_FILE: append_color(sb, "#D8DEF0", 0, full); break;
        case FS_FILE_HIDDEN: append_color(sb, "#666D88", 0, full); break;
        case FS_NONE: append_color(sb, "#FF7B8A", 1, full); break;
    }
}

static int is_numeric(const char *s) {
    size_t i = 0;
    size_t len = strlen(s);
    if (len == 0) {
        return 0;
    }
    if (s[0] == '-') {
        i = 1;
    }
    if (i == len) {
        return 0;
    }
    size_t digits_before = 0;
    while (i < len && isdigit((unsigned char)s[i])) {
        i++;
        digits_before++;
    }
    if (digits_before == 0) {
        return 0;
    }
    if (i == len) {
        return 1;
    }
    if (s[i] != '.') {
        return 0;
    }
    i++;
    size_t digits_after = 0;
    while (i < len && isdigit((unsigned char)s[i])) {
        i++;
        digits_after++;
    }
    return digits_after > 0 && i == len;
}

static void color_arg(StrBuf *sb, const char *word, const char *prev_flag) {
    if (is_numeric(word)) {
        append_color(sb, "#FF9E64", 0, word);
        return;
    }

    if (prev_flag[0] != '\0' && str_in_set(prev_flag, LONG_FLAGS_WITH_VALUES, COUNT(LONG_FLAGS_WITH_VALUES))) {
        append_color(sb, "#FFB347", 0, word);
        return;
    }

    int hidden;
    FsKind kind = resolve_fs_kind(word, &hidden);
    path_arg_color(sb, word, kind);
}

static long long hl_millis(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static char **g_exec_cache = NULL;
static size_t g_exec_cache_count = 0;
static long long g_exec_cache_time = 0;
#define EXEC_CACHE_TTL_MS 5000

static int exec_cmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void refresh_exec_cache(void) {
    for (size_t i = 0; i < g_exec_cache_count; i++) {
        free(g_exec_cache[i]);
    }
    free(g_exec_cache);
    g_exec_cache = NULL;
    g_exec_cache_count = 0;
    size_t capacity = 0;

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
                    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                        continue;
                    }
                    if (g_exec_cache_count == capacity) {
                        capacity = capacity ? capacity * 2 : 128;
                        g_exec_cache = realloc(g_exec_cache, capacity * sizeof(char *));
                    }
                    g_exec_cache[g_exec_cache_count++] = strdup(entry->d_name);
                }
                closedir(dp);
            }
            dir = strtok_r(NULL, ":", &saveptr);
        }
        free(copy);
    }

    qsort(g_exec_cache, g_exec_cache_count, sizeof(char *), exec_cmp);
    g_exec_cache_time = hl_millis();
}

void highlight_invalidate_exec_cache(void) {
    refresh_exec_cache();
}

static int exec_cache_has(const char *name) {
    if (g_exec_cache_time == 0 || hl_millis() - g_exec_cache_time > EXEC_CACHE_TTL_MS) {
        refresh_exec_cache();
    }
    if (g_exec_cache_count == 0) {
        return 0;
    }
    return bsearch(&name, g_exec_cache, g_exec_cache_count, sizeof(char *), exec_cmp) != NULL;
}

static int command_exists(const char *cmd) {
    if (cmd[0] == '\0') {
        return 0;
    }
    const char *base = hl_basename(cmd);
    if (str_in_set(base, BUILTINS, COUNT(BUILTINS))) {
        return 1;
    }
    if (alias_get(base)) {
        return 1;
    }
    if (cmd[0] == '/' || strncmp(cmd, "./", 2) == 0 || strncmp(cmd, "../", 3) == 0) {
        return access(cmd, X_OK) == 0;
    }
    return exec_cache_has(base);
}

typedef enum {
    HL_COMMAND, HL_SUBCOMMAND, HL_ARG, HL_FLAG, HL_OPERATOR, HL_REDIRECT,
    HL_STRING_D, HL_STRING_S, HL_VARIABLE, HL_INCOMPLETE_S, HL_NUMBER
} HlTokenType;

typedef struct {
    HlTokenType type;
    char *value;
} HlToken;

typedef struct {
    HlToken *items;
    size_t count;
    size_t capacity;
} HlTokenList;

static void hl_push(HlTokenList *list, HlTokenType type, const char *value) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 16;
        list->items = realloc(list->items, list->capacity * sizeof(HlToken));
    }
    list->items[list->count].type = type;
    list->items[list->count].value = strdup(value);
    list->count++;
}

static void hl_push_char(HlTokenList *list, HlTokenType type, char c) {
    char buf[2] = {c, '\0'};
    hl_push(list, type, buf);
}

static HlTokenList tokenize_for_highlight(const char *input) {
    HlTokenList list = {0};
    size_t i = 0;
    size_t len = strlen(input);
    int expect_cmd = 1;
    char last_cmd[4096] = {0};
    size_t real_token_count = 0;

    while (i < len) {
        char ch = input[i];

        if (ch == ' ' || ch == '\t') {
            hl_push_char(&list, HL_ARG, ch);
            i++;
            continue;
        }

        if (ch == '&' && input[i + 1] == '&') { hl_push(&list, HL_OPERATOR, "&&"); i += 2; expect_cmd = 1; last_cmd[0] = '\0'; real_token_count++; continue; }
        if (ch == '|' && input[i + 1] == '|') { hl_push(&list, HL_OPERATOR, "||"); i += 2; expect_cmd = 1; last_cmd[0] = '\0'; real_token_count++; continue; }
        if (ch == '|') { hl_push(&list, HL_OPERATOR, "|"); i++; expect_cmd = 1; last_cmd[0] = '\0'; real_token_count++; continue; }
        if (ch == ';') { hl_push(&list, HL_OPERATOR, ";"); i++; expect_cmd = 1; last_cmd[0] = '\0'; real_token_count++; continue; }
        if (ch == '&') { hl_push(&list, HL_OPERATOR, "&"); i++; real_token_count++; continue; }

        if (ch == '>' && input[i + 1] == '>') { hl_push(&list, HL_REDIRECT, ">>"); i += 2; real_token_count++; continue; }
        if (ch == '>') { hl_push(&list, HL_REDIRECT, ">"); i++; real_token_count++; continue; }
        if (ch == '<') { hl_push(&list, HL_REDIRECT, "<"); i++; real_token_count++; continue; }

        if (ch == '"') {
            StrBuf sb;
            strbuf_init(&sb);
            strbuf_push_char(&sb, '"');
            i++;
            while (i < len && input[i] != '"') {
                if (input[i] == '\\' && i + 1 < len) {
                    strbuf_push_char(&sb, input[i]);
                    strbuf_push_char(&sb, input[i + 1]);
                    i += 2;
                } else {
                    strbuf_push_char(&sb, input[i]);
                    i++;
                }
            }
            if (i < len) {
                strbuf_push_char(&sb, '"');
                i++;
                char *s = strbuf_take(&sb);
                hl_push(&list, HL_STRING_D, s);
                free(s);
            } else {
                char *s = strbuf_take(&sb);
                hl_push(&list, HL_INCOMPLETE_S, s);
                free(s);
            }
            real_token_count++;
            continue;
        }

        if (ch == '\'') {
            StrBuf sb;
            strbuf_init(&sb);
            strbuf_push_char(&sb, '\'');
            i++;
            while (i < len && input[i] != '\'') {
                strbuf_push_char(&sb, input[i]);
                i++;
            }
            if (i < len) {
                strbuf_push_char(&sb, '\'');
                i++;
                char *s = strbuf_take(&sb);
                hl_push(&list, HL_STRING_S, s);
                free(s);
            } else {
                char *s = strbuf_take(&sb);
                hl_push(&list, HL_INCOMPLETE_S, s);
                free(s);
            }
            real_token_count++;
            continue;
        }

        if (ch == '$') {
            StrBuf sb;
            strbuf_init(&sb);
            strbuf_push_char(&sb, '$');
            i++;
            while (i < len && (isalnum((unsigned char)input[i]) || input[i] == '_' || input[i] == '?')) {
                strbuf_push_char(&sb, input[i]);
                i++;
            }
            char *s = strbuf_take(&sb);
            hl_push(&list, HL_VARIABLE, s);
            free(s);
            real_token_count++;
            continue;
        }

        StrBuf word;
        strbuf_init(&word);
        while (i < len && input[i] != ' ' && input[i] != '\t' && input[i] != '|' &&
               input[i] != '>' && input[i] != '<' && input[i] != ';' && input[i] != '&' &&
               input[i] != '"' && input[i] != '\'') {
            strbuf_push_char(&word, input[i]);
            i++;
        }

        if (word.length == 0) {
            strbuf_free(&word);
            i++;
            continue;
        }

        char *w = strbuf_take(&word);

        if (expect_cmd) {
            hl_push(&list, HL_COMMAND, w);
            snprintf(last_cmd, sizeof(last_cmd), "%s", w);
            expect_cmd = 0;
            real_token_count++;
        } else if (w[0] == '-') {
            hl_push(&list, HL_FLAG, w);
            real_token_count++;
        } else {
            int is_first_arg = real_token_count == 1;
            int is_subcommand = 0;

            if (is_first_arg && last_cmd[0] != '\0' && w[0] != '-' && strchr(w, '/') == NULL) {
                const char *base = hl_basename(last_cmd);
                if ((str_in_set(base, CMD_GIT, COUNT(CMD_GIT)) && str_in_set(w, GIT_SUBCOMMANDS, COUNT(GIT_SUBCOMMANDS))) ||
                    (str_in_set(base, CMD_NODE, COUNT(CMD_NODE)) && str_in_set(w, NPM_SUBCOMMANDS, COUNT(NPM_SUBCOMMANDS))) ||
                    (str_in_set(base, CMD_DOCKER, COUNT(CMD_DOCKER)) && str_in_set(w, DOCKER_SUBCOMMANDS, COUNT(DOCKER_SUBCOMMANDS))) ||
                    (str_in_set(base, SUDO_LIKE, COUNT(SUDO_LIKE)) && command_exists(w))) {
                    is_subcommand = 1;
                }
            }

            if (is_subcommand) {
                hl_push(&list, HL_SUBCOMMAND, w);
                real_token_count++;
            } else if (is_numeric(w)) {
                hl_push(&list, HL_NUMBER, w);
                real_token_count++;
            } else {
                hl_push(&list, HL_ARG, w);
                real_token_count++;
            }
        }

        free(w);
    }

    return list;
}

char *highlight_render(const char *input) {
    HlTokenList tokens = tokenize_for_highlight(input);

    StrBuf out;
    strbuf_init(&out);

    char last_cmd[4096] = {0};
    char last_flag[512] = {0};

    for (size_t i = 0; i < tokens.count; i++) {
        HlToken *tok = &tokens.items[i];
        switch (tok->type) {
            case HL_COMMAND:
                snprintf(last_cmd, sizeof(last_cmd), "%s", tok->value);
                last_flag[0] = '\0';
                if (command_exists(tok->value)) {
                    cmd_color(&out, tok->value);
                } else {
                    append_color(&out, "#FF6B7A", 0, tok->value);
                }
                break;
            case HL_SUBCOMMAND:
                subcommand_color(&out, last_cmd, tok->value);
                break;
            case HL_FLAG:
                snprintf(last_flag, sizeof(last_flag), "%s", tok->value);
                flag_color(&out, tok->value);
                break;
            case HL_NUMBER:
                append_color(&out, "#FF9E64", 0, tok->value);
                break;
            case HL_OPERATOR:
                append_color(&out, "#56D4D4", 0, tok->value);
                break;
            case HL_REDIRECT:
                append_color(&out, "#F0A05A", 0, tok->value);
                break;
            case HL_STRING_D:
                append_color(&out, "#E8A062", 0, tok->value);
                break;
            case HL_STRING_S:
                append_color(&out, "#A8D672", 0, tok->value);
                break;
            case HL_INCOMPLETE_S:
                append_color(&out, "#C07840", 0, tok->value);
                break;
            case HL_VARIABLE:
                append_color(&out, "#E070C8", 0, tok->value);
                break;
            case HL_ARG:
                if (strcmp(tok->value, " ") == 0 || strcmp(tok->value, "\t") == 0) {
                    last_flag[0] = '\0';
                    strbuf_push_str(&out, tok->value);
                } else {
                    color_arg(&out, tok->value, last_flag);
                    last_flag[0] = '\0';
                }
                break;
        }
    }

    for (size_t i = 0; i < tokens.count; i++) {
        free(tokens.items[i].value);
    }
    free(tokens.items);

    return strbuf_take(&out);
}
