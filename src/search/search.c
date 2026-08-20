#include "search/search.h"

#include "bookmarks/bookmarks.h"
#include "env/alias.h"
#include "fileops/trash.h"
#include "input/history.h"
#include "platform/platform.h"
#include "tui/tui.h"
#include "util/strbuf.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef enum { RES_HISTORY, RES_FILE, RES_DIR, RES_EXECUTABLE, RES_BUILTIN, RES_ALIAS, RES_KIND_COUNT } ResultKind;

typedef struct {
    ResultKind kind;
    char value[1024];
    char display[1200];
    char sub[1024];
    char full_path[4096];
} SearchMatch;

typedef struct {
    SearchMatch *items;
    size_t count;
    size_t capacity;
} MatchList;

static void ml_push(MatchList *list, SearchMatch m) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 32;
        list->items = realloc(list->items, list->capacity * sizeof(SearchMatch));
    }
    list->items[list->count++] = m;
}

static void ml_free(MatchList *list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static const char *const SEARCH_BUILTINS[] = {
    "exit", "echo", "type", "pwd", "cd", "ls", "dir", "alias", "unalias",
    "clear", "history", "trash", "fshrc", "neofetch",
};
#define SEARCH_BUILTINS_COUNT (sizeof(SEARCH_BUILTINS) / sizeof(SEARCH_BUILTINS[0]))

static const char *const EDITOR_CANDIDATES[] = {
    "nvim", "vim", "vi", "nano", "emacs", "micro", "hx", "helix", "code", "gedit",
};
#define EDITOR_CANDIDATES_COUNT (sizeof(EDITOR_CANDIDATES) / sizeof(EDITOR_CANDIDATES[0]))

static const char *const SKIP_DIRS[] = {
    "node_modules", ".git", ".svn", ".hg", "dist", "build", "out", ".next", ".nuxt",
    "__pycache__", ".pytest_cache", ".mypy_cache", ".cache", ".npm", ".yarn", "proc", "sys", "dev",
};
#define SKIP_DIRS_COUNT (sizeof(SKIP_DIRS) / sizeof(SKIP_DIRS[0]))

static void to_lower_copy(const char *s, char *out, size_t out_size) {
    size_t i = 0;
    for (; s[i] && i + 1 < out_size; i++) {
        out[i] = (char)tolower((unsigned char)s[i]);
    }
    out[i] = '\0';
}

static void strip_chars(const char *s, const char *remove_set, char *out, size_t out_size) {
    size_t j = 0;
    for (size_t i = 0; s[i] && j + 1 < out_size; i++) {
        if (!strchr(remove_set, s[i])) {
            out[j++] = s[i];
        }
    }
    out[j] = '\0';
}

static size_t split_ws(const char *s, char tokens[][256], size_t max_tokens) {
    size_t count = 0;
    const char *p = s;
    while (*p && count < max_tokens) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len >= 256) {
            len = 255;
        }
        memcpy(tokens[count], start, len);
        tokens[count][len] = '\0';
        count++;
    }
    return count;
}

static int fuzzy_score(const char *query, const char *target) {
    if (query[0] == '\0') {
        return 1;
    }
    char q[512];
    char t[512];
    to_lower_copy(query, q, sizeof(q));
    to_lower_copy(target, t, sizeof(t));

    char t_norm[512];
    strip_chars(t, "-_ .", t_norm, sizeof(t_norm));
    char q_norm[512];
    strip_chars(q, " .", q_norm, sizeof(q_norm));

    char tokens[16][256];
    size_t token_count = split_ws(q, tokens, 16);
    if (token_count == 0) {
        return 1;
    }

    int total_score = 0;
    int matches_all = 1;

    for (size_t i = 0; i < token_count; i++) {
        const char *token = tokens[i];
        if (strstr(t, token) || strstr(t_norm, token)) {
            total_score += 20;

            char dash_tok[280];
            char us_tok[280];
            char dot_tok[280];
            snprintf(dash_tok, sizeof(dash_tok), "-%s", token);
            snprintf(us_tok, sizeof(us_tok), "_%s", token);
            snprintf(dot_tok, sizeof(dot_tok), ".%s", token);

            if (strncmp(t, token, strlen(token)) == 0 || strstr(t, dash_tok) || strstr(t, us_tok) ||
                strstr(t, dot_tok)) {
                total_score += 30;
            }
        } else {
            matches_all = 0;
            break;
        }
    }

    if (!matches_all) {
        return 0;
    }
    if (strstr(t_norm, q_norm)) {
        total_score += 30;
    }
    return total_score;
}

static void shorten_path(const char *p, char *out, size_t out_size) {
    const char *home = getenv("HOME");
    if (home && home[0] != '\0' && strncmp(p, home, strlen(home)) == 0) {
        snprintf(out, out_size, "~%s", p + strlen(home));
    } else {
        snprintf(out, out_size, "%s", p);
    }
}

typedef struct {
    SearchMatch m;
    int score;
} ScoredMatch;

typedef struct {
    ScoredMatch *items;
    size_t count;
    size_t capacity;
} ScoredList;

static void sl_push(ScoredList *list, SearchMatch m, int score) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 64;
        list->items = realloc(list->items, list->capacity * sizeof(ScoredMatch));
    }
    ScoredMatch sm = {m, score};
    list->items[list->count++] = sm;
}

static int scored_cmp_desc(const void *ap, const void *bp) {
    const ScoredMatch *a = ap;
    const ScoredMatch *b = bp;
    return b->score - a->score;
}

typedef struct {
    ScoredList *results;
    char (*visited)[4096];
    size_t *visited_count;
    int max_results;
    const char *query;
} WalkCtx;

static void fs_walk(WalkCtx *ctx, const char *dir, int depth) {
    if (depth > 4 || ctx->results->count > (size_t)(ctx->max_results * 2)) {
        return;
    }
    for (size_t i = 0; i < *ctx->visited_count; i++) {
        if (strcmp(ctx->visited[i], dir) == 0) {
            return;
        }
    }
    if (*ctx->visited_count < 256) {
        snprintf(ctx->visited[*ctx->visited_count], 4096, "%s", dir);
        (*ctx->visited_count)++;
    }

    DIR *dp = opendir(dir);
    if (!dp) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (entry->d_name[0] == '.' && depth > 1) {
            continue;
        }

        int score = fuzzy_score(ctx->query, entry->d_name);
        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", dir, entry->d_name);

        struct stat st;
        int is_dir = 0;
        if (lstat(full, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
        }

        if (score > 0) {
            SearchMatch m;
            memset(&m, 0, sizeof(m));
            m.kind = is_dir ? RES_DIR : RES_FILE;
            snprintf(m.value, sizeof(m.value), "%s", entry->d_name);
            snprintf(m.display, sizeof(m.display), "%s%s", entry->d_name, is_dir ? "/" : "");
            shorten_path(dir, m.sub, sizeof(m.sub));
            snprintf(m.full_path, sizeof(m.full_path), "%s", full);
            sl_push(ctx->results, m, score);
        }

        if (is_dir) {
            int skip = 0;
            for (size_t s = 0; s < SKIP_DIRS_COUNT; s++) {
                if (strcmp(entry->d_name, SKIP_DIRS[s]) == 0) {
                    skip = 1;
                    break;
                }
            }
            if (!skip) {
                fs_walk(ctx, full, depth + 1);
            }
        }
    }
    closedir(dp);
}

static void search_filesystem(const char *query, const char *const *root_dirs, size_t root_count, int max_results,
                               MatchList *out) {
    ScoredList results = {0};
    char visited[256][4096];
    size_t visited_count = 0;

    WalkCtx ctx = {&results, visited, &visited_count, max_results, query};

    for (size_t i = 0; i < root_count; i++) {
        fs_walk(&ctx, root_dirs[i], 0);
    }

    qsort(results.items, results.count, sizeof(ScoredMatch), scored_cmp_desc);
    size_t limit = results.count < (size_t)max_results ? results.count : (size_t)max_results;
    for (size_t i = 0; i < limit; i++) {
        ml_push(out, results.items[i].m);
    }
    free(results.items);
}

static void search_history(const char *query, MatchList *out) {
    ScoredList results = {0};
    size_t count = history_count();
    for (size_t i = 0; i < count; i++) {
        char cmd[4096];
        long long ts;
        if (!history_entry_at(i, cmd, sizeof(cmd), &ts)) {
            continue;
        }
        int score = fuzzy_score(query, cmd);
        if (score <= 0) {
            continue;
        }
        SearchMatch m;
        memset(&m, 0, sizeof(m));
        m.kind = RES_HISTORY;
        snprintf(m.value, sizeof(m.value), "%s", cmd);
        snprintf(m.display, sizeof(m.display), "%s", cmd);
        if (ts != 0) {
            time_t sec = (time_t)(ts / 1000);
            struct tm tmv;
            localtime_r(&sec, &tmv);
            strftime(m.sub, sizeof(m.sub), "%b %e %H:%M", &tmv);
        }
        sl_push(&results, m, score);
    }
    qsort(results.items, results.count, sizeof(ScoredMatch), scored_cmp_desc);
    size_t limit = results.count < 20 ? results.count : 20;
    for (size_t i = 0; i < limit; i++) {
        ml_push(out, results.items[i].m);
    }
    free(results.items);
}

static void search_executables(const char *query, MatchList *out) {
    if (strlen(query) < 2) {
        return;
    }
    ScoredList results = {0};
    char seen[512][256];
    size_t seen_count = 0;

    const char *path_env = getenv("PATH");
    if (!path_env) {
        return;
    }
    char *copy = strdup(path_env);
    char *saveptr = NULL;
    char *dir = strtok_r(copy, ":", &saveptr);
    while (dir) {
        DIR *dp = opendir(dir);
        if (dp) {
            struct dirent *entry;
            while ((entry = readdir(dp)) != NULL) {
                int score = fuzzy_score(query, entry->d_name);
                if (score <= 0) {
                    continue;
                }
                int already = 0;
                for (size_t i = 0; i < seen_count; i++) {
                    if (strcmp(seen[i], entry->d_name) == 0) {
                        already = 1;
                        break;
                    }
                }
                if (already) {
                    continue;
                }
                if (seen_count < 512) {
                    snprintf(seen[seen_count], sizeof(seen[seen_count]), "%s", entry->d_name);
                    seen_count++;
                }
                SearchMatch m;
                memset(&m, 0, sizeof(m));
                m.kind = RES_EXECUTABLE;
                snprintf(m.value, sizeof(m.value), "%s", entry->d_name);
                snprintf(m.display, sizeof(m.display), "%s", entry->d_name);
                shorten_path(dir, m.sub, sizeof(m.sub));
                snprintf(m.full_path, sizeof(m.full_path), "%s/%s", dir, entry->d_name);
                sl_push(&results, m, score);
            }
            closedir(dp);
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }
    free(copy);

    qsort(results.items, results.count, sizeof(ScoredMatch), scored_cmp_desc);
    size_t limit = results.count < 15 ? results.count : 15;
    for (size_t i = 0; i < limit; i++) {
        ml_push(out, results.items[i].m);
    }
    free(results.items);
}

static void search_builtins(const char *query, MatchList *out) {
    for (size_t i = 0; i < SEARCH_BUILTINS_COUNT; i++) {
        if (fuzzy_score(query, SEARCH_BUILTINS[i]) > 0) {
            SearchMatch m;
            memset(&m, 0, sizeof(m));
            m.kind = RES_BUILTIN;
            snprintf(m.value, sizeof(m.value), "%s", SEARCH_BUILTINS[i]);
            snprintf(m.display, sizeof(m.display), "%s", SEARCH_BUILTINS[i]);
            snprintf(m.sub, sizeof(m.sub), "fsh builtin");
            ml_push(out, m);
        }
    }
}

static void search_aliases(const char *query, MatchList *out) {
    size_t count;
    const AliasEntry *aliases = alias_list(&count);
    for (size_t i = 0; i < count; i++) {
        if (fuzzy_score(query, aliases[i].name) > 0) {
            SearchMatch m;
            memset(&m, 0, sizeof(m));
            m.kind = RES_ALIAS;
            snprintf(m.value, sizeof(m.value), "%s", aliases[i].name);
            snprintf(m.display, sizeof(m.display), "%s", aliases[i].name);
            snprintf(m.sub, sizeof(m.sub), "%s", aliases[i].value);
            ml_push(out, m);
        }
    }
}

static const char *CATEGORY_LABEL[RES_KIND_COUNT] = {
    "Command history", "Files", "Directories", "Executables", "Builtins", "Aliases",
};
static const char *CATEGORY_ICON[RES_KIND_COUNT] = {
    "  ", "  ", "\xe2\x96\xb8 ", "  ", "  ", "\xe2\x9a\xa1 ",
};
static const ResultKind CATEGORY_ORDER[] = {RES_HISTORY, RES_DIR, RES_FILE, RES_BUILTIN, RES_ALIAS, RES_EXECUTABLE};
#define CATEGORY_ORDER_COUNT (sizeof(CATEGORY_ORDER) / sizeof(CATEGORY_ORDER[0]))

typedef enum { ROW_HEADER, ROW_RESULT } SrRowKind;

typedef struct {
    SrRowKind kind;
    ResultKind cat;
    size_t match_idx;
    size_t count;
} SrRow;

typedef struct {
    MatchList buckets[RES_KIND_COUNT];
} Grouped;

static void grouped_free(Grouped *g) {
    for (int i = 0; i < RES_KIND_COUNT; i++) {
        ml_free(&g->buckets[i]);
    }
}

static void run_search(const char *query, Grouped *g) {
    grouped_free(g);
    memset(g, 0, sizeof(*g));

    if (query[0] == '\0') {
        size_t count = history_count();
        size_t limit = count < 30 ? count : 30;
        for (size_t i = 0; i < limit; i++) {
            char cmd[4096];
            long long ts;
            if (!history_entry_at(i, cmd, sizeof(cmd), &ts)) {
                continue;
            }
            SearchMatch m;
            memset(&m, 0, sizeof(m));
            m.kind = RES_HISTORY;
            snprintf(m.value, sizeof(m.value), "%s", cmd);
            snprintf(m.display, sizeof(m.display), "%s", cmd);
            if (ts != 0) {
                time_t sec = (time_t)(ts / 1000);
                struct tm tmv;
                localtime_r(&sec, &tmv);
                strftime(m.sub, sizeof(m.sub), "%b %e %H:%M", &tmv);
            }
            ml_push(&g->buckets[RES_HISTORY], m);
        }
        return;
    }

    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        snprintf(cwd, sizeof(cwd), "/");
    }
    const char *home = getenv("HOME");
    const char *roots[2];
    size_t root_count = 0;
    roots[root_count++] = cwd;
    if (home && strcmp(home, cwd) != 0) {
        roots[root_count++] = home;
    }

    search_history(query, &g->buckets[RES_HISTORY]);

    MatchList fs_results = {0};
    search_filesystem(query, roots, root_count, 40, &fs_results);
    for (size_t i = 0; i < fs_results.count; i++) {
        if (fs_results.items[i].kind == RES_DIR) {
            ml_push(&g->buckets[RES_DIR], fs_results.items[i]);
        } else {
            ml_push(&g->buckets[RES_FILE], fs_results.items[i]);
        }
    }
    ml_free(&fs_results);

    search_executables(query, &g->buckets[RES_EXECUTABLE]);
    search_builtins(query, &g->buckets[RES_BUILTIN]);
    search_aliases(query, &g->buckets[RES_ALIAS]);
}

static SrRow *build_rows(Grouped *g, size_t *out_count) {
    size_t capacity = 128;
    SrRow *rows = malloc(capacity * sizeof(SrRow));
    size_t count = 0;

    for (size_t c = 0; c < CATEGORY_ORDER_COUNT; c++) {
        ResultKind cat = CATEGORY_ORDER[c];
        MatchList *bucket = &g->buckets[cat];
        if (bucket->count == 0) {
            continue;
        }
        if (count == capacity) {
            capacity *= 2;
            rows = realloc(rows, capacity * sizeof(SrRow));
        }
        SrRow header = {ROW_HEADER, cat, 0, bucket->count};
        rows[count++] = header;

        for (size_t i = 0; i < bucket->count; i++) {
            if (count == capacity) {
                capacity *= 2;
                rows = realloc(rows, capacity * sizeof(SrRow));
            }
            SrRow r = {ROW_RESULT, cat, i, 0};
            rows[count++] = r;
        }
    }

    *out_count = count;
    return rows;
}

static int read_raw_byte(void) {
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) {
        return -1;
    }
    return (unsigned char)c;
}

static int vis_rows(void) {
    int r = tui_rows() - 2 - 3;
    return r > 1 ? r : 1;
}

static const char *kind_color_code(ResultKind kind, int hidden) {
    switch (kind) {
        case RES_HISTORY: return "37";
        case RES_DIR: return hidden ? "36" : "34;1";
        case RES_FILE: return hidden ? "90" : "37";
        case RES_BUILTIN: return "32;1";
        case RES_ALIAS: return "32";
        case RES_EXECUTABLE: return "38;2;195;232;141";
        default: return "37";
    }
}

static void draw_search_bar(const char *query, size_t cursor_pos) {
    int row = 3;

    StrBuf out;
    strbuf_init(&out);
    char buf[64];
    snprintf(buf, sizeof(buf), "\x1b[%d;1H\x1b[2K", row);
    strbuf_push_str(&out, buf);
    strbuf_push_str(&out, "\x1b[40;37m search \x1b[0m ");

    size_t qlen = strlen(query);
    for (size_t i = 0; i < qlen; i++) {
        if (i == cursor_pos) {
            char cbuf[8];
            snprintf(cbuf, sizeof(cbuf), "%c", query[i]);
            strbuf_push_str(&out, "\x1b[47;30m");
            strbuf_push_str(&out, cbuf);
            strbuf_push_str(&out, "\x1b[0m");
        } else {
            strbuf_push_char(&out, query[i]);
        }
    }
    if (cursor_pos >= qlen) {
        strbuf_push_str(&out, "\x1b[47;30m \x1b[0m");
    }

    fputs(out.data, stdout);
    strbuf_free(&out);
}

static void draw_results(Grouped *g, SrRow *rows, size_t row_count, size_t sel, size_t scroll) {
    int start = 6;
    int v = vis_rows();
    int cols = tui_cols();

    for (int i = 0; i < v; i++) {
        printf("\x1b[%d;1H\x1b[2K", start + i);
        size_t idx = scroll + (size_t)i;
        if (idx >= row_count) {
            continue;
        }
        const SrRow *row = &rows[idx];
        int is_active = (idx == sel);

        if (row->kind == ROW_HEADER) {
            char raw[128];
            snprintf(raw, sizeof(raw), "  %s  (%zu)", CATEGORY_LABEL[row->cat], row->count);
            char *padded = tui_pad_or_trim(raw, cols);
            if (is_active) {
                printf("\x1b[43;30;1m%s\x1b[0m", padded);
            } else {
                printf("\x1b[33;1m%s\x1b[0m", padded);
            }
            free(padded);
        } else {
            const SearchMatch *m = &g->buckets[row->cat].items[row->match_idx];
            int hidden = m->display[0] == '.';
            const char *icon = CATEGORY_ICON[row->cat];
            int sub_len = (int)tui_visible_len(m->sub);
            int max_disp = cols - (int)strlen(icon) - sub_len - 6;
            if (max_disp < 8) {
                max_disp = 8;
            }

            char display[1200];
            snprintf(display, sizeof(display), "%s", m->display);
            if ((int)strlen(display) > max_disp) {
                display[max_disp - 1] = '\0';
                strcat(display, "\xe2\x80\xa6");
            }

            char raw_left[1400];
            snprintf(raw_left, sizeof(raw_left), "  %s%s", icon, display);
            int left_w = cols - sub_len - 2;
            if (left_w < 4) {
                left_w = 4;
            }
            char *padded = tui_pad_or_trim(raw_left, left_w);

            if (is_active) {
                printf("\x1b[47;30;1m%s  %s\x1b[0m", padded, m->sub);
            } else {
                printf("\x1b[%sm%s\x1b[0m  \x1b[2m%s\x1b[0m", kind_color_code(row->cat, hidden), padded, m->sub);
            }
            free(padded);
        }
    }

    if (row_count == 0) {
        printf("\x1b[%d;1H\x1b[2K\x1b[2m  (no results)\x1b[0m", start);
    }
}

static void render(Grouped *g, SrRow *rows, size_t row_count, size_t sel, size_t scroll, const char *query,
                    size_t cursor_pos) {
    tui_clear_screen();

    NavRows nr;
    navrows_init(&nr);
    size_t r0 = navrows_add_row(&nr);
    navrows_add_item(&nr, r0, "Nav", "Navigate", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "Ent", "Select", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "Esc", "Cancel", NAV_DEFAULT);
    tui_draw_navbar(&nr);
    navrows_free(&nr);

    draw_search_bar(query, cursor_pos);
    draw_results(g, rows, row_count, sel, scroll);

    size_t total_results = 0;
    for (size_t i = 0; i < row_count; i++) {
        if (rows[i].kind == ROW_RESULT) {
            total_results++;
        }
    }
    char left[64];
    snprintf(left, sizeof(left), "Search  %zu result%s", total_results, total_results == 1 ? "" : "s");
    tui_draw_footer((int)row_count, (int)scroll, vis_rows(), left);
    fflush(stdout);
}

static void adjust_scroll(size_t sel, size_t *scroll) {
    int v = vis_rows();
    if (sel < *scroll) {
        *scroll = sel;
    }
    if (sel >= *scroll + (size_t)v) {
        *scroll = sel - (size_t)v + 1;
    }
}

static void show_delete_confirm(const SearchMatch *m) {
    const char *body[] = {m->display};
    TuiPopupResult r = tui_popup_input("Move to trash?", NULL, 0, body, 1, NULL, NULL);
    if (r.confirmed) {
        char *err = trash_move_to_trash(m->full_path, NULL);
        free(err);
    }
}

static void show_dir_action(const SearchMatch *m, int *do_cd) {
    for (;;) {
        tui_clear_screen();
        NavRows nr;
        navrows_init(&nr);
        size_t r0 = navrows_add_row(&nr);
        navrows_add_item(&nr, r0, "Ent", "cd into", NAV_DEFAULT);
        navrows_add_item(&nr, r0, "D", "Delete", NAV_RED);
        navrows_add_item(&nr, r0, "Esc", "Back", NAV_DEFAULT);
        tui_draw_navbar(&nr);
        navrows_free(&nr);

        int start = 4;
        int avail = tui_rows() - 2 - 2;
        int cols = tui_cols();
        int ln = 0;

        printf("\x1b[%d;1H\x1b[2K\x1b[34;1m\xe2\x96\xb8 %s\x1b[0m  \x1b[2m%s\x1b[0m", start + ln, m->display, m->sub);
        ln++;
        char sep[256];
        int seplen = cols - 2 < 60 ? cols - 2 : 60;
        if (seplen < 0) {
            seplen = 0;
        }
        memset(sep, '-', (size_t)seplen);
        sep[seplen] = '\0';
        printf("\x1b[%d;1H\x1b[2K\x1b[2m%s\x1b[0m", start + ln, sep);
        ln++;

        DIR *dp = opendir(m->full_path);
        if (!dp) {
            printf("\x1b[%d;1H\x1b[2K\x1b[31m  cannot read directory\x1b[0m", start + ln);
            ln++;
        } else {
            struct dirent *entry;
            int shown = 0;
            int total = 0;
            char names[256][256];
            int is_dirs[256];
            while ((entry = readdir(dp)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                if (total < 256) {
                    snprintf(names[total], sizeof(names[total]), "%s", entry->d_name);
                    char full[4096];
                    snprintf(full, sizeof(full), "%s/%s", m->full_path, entry->d_name);
                    struct stat st;
                    is_dirs[total] = (stat(full, &st) == 0) && S_ISDIR(st.st_mode);
                }
                total++;
            }
            closedir(dp);

            if (total == 0) {
                printf("\x1b[%d;1H\x1b[2K\x1b[2m  (empty directory)\x1b[0m", start + ln);
                ln++;
            } else {
                int max_show = avail - ln - 1;
                for (int i = 0; i < total && i < max_show; i++) {
                    printf("\x1b[%d;1H\x1b[2K  %s%s\x1b[0m", start + ln, is_dirs[i] ? "\x1b[34m\xe2\x96\xb8 " : "  ",
                           names[i]);
                    ln++;
                    shown++;
                }
                if (total > shown) {
                    printf("\x1b[%d;1H\x1b[2K\x1b[2m  ... and %d more\x1b[0m", start + ln, total - shown);
                    ln++;
                }
            }
        }

        for (int i = ln; i < avail; i++) {
            printf("\x1b[%d;1H\x1b[2K", start + i);
        }
        tui_draw_bottom_bar(m->display, "");
        fflush(stdout);

        int c = read_raw_byte();
        if (c < 0) {
            continue;
        }
        if (c == 0x1b) {
            int c2 = tui_read_byte_after_esc();
            if (c2 == '[') {
                read_raw_byte();
                continue;
            }
            *do_cd = 0;
            return;
        }
        if (c == 3) {
            *do_cd = 0;
            return;
        }
        if (c == '\r' || c == '\n') {
            *do_cd = 1;
            return;
        }
        if (c == 'd' || c == 'D') {
            show_delete_confirm(m);
            *do_cd = 0;
            return;
        }
    }
}

static void show_file_action(const SearchMatch *m, char *chosen_cmd_out, size_t chosen_out_size, int *opened) {
    *opened = 0;
    int preview_scroll = 0;
    int in_editor_picker = 0;
    size_t editor_count = 0;
    const char *installed[EDITOR_CANDIDATES_COUNT];
    for (size_t i = 0; i < EDITOR_CANDIDATES_COUNT; i++) {
        const char *path_env = getenv("PATH");
        if (!path_env) {
            continue;
        }
        char *copy = strdup(path_env);
        char *saveptr = NULL;
        char *dir = strtok_r(copy, ":", &saveptr);
        int found = 0;
        while (dir) {
            char full[4096];
            snprintf(full, sizeof(full), "%s/%s", dir, EDITOR_CANDIDATES[i]);
            if (access(full, X_OK) == 0) {
                found = 1;
                break;
            }
            dir = strtok_r(NULL, ":", &saveptr);
        }
        free(copy);
        if (found) {
            installed[editor_count++] = EDITOR_CANDIDATES[i];
        }
    }
    if (editor_count == 0) {
        installed[editor_count++] = "vi";
    }
    size_t e_sel = 0;

    for (;;) {
        tui_clear_screen();
        NavRows nr;
        navrows_init(&nr);
        size_t r0 = navrows_add_row(&nr);
        navrows_add_item(&nr, r0, "Up/Dn", "Scroll", NAV_DEFAULT);
        navrows_add_item(&nr, r0, "Ent", "Open Editor", NAV_DEFAULT);
        navrows_add_item(&nr, r0, "D", "Delete", NAV_RED);
        navrows_add_item(&nr, r0, "Esc", "Back", NAV_DEFAULT);
        tui_draw_navbar(&nr);
        navrows_free(&nr);

        int cols = tui_cols();
        int rows_count = tui_rows();
        int start = 3;
        int avail = rows_count - 6;
        int ln = 0;

        struct stat st;
        char size_str[32] = "?";
        char mod_str[32] = "?";
        if (stat(m->full_path, &st) == 0) {
            snprintf(size_str, sizeof(size_str), "%.1f KB", (double)st.st_size / 1024.0);
            struct tm tmv;
            localtime_r(&st.st_mtime, &tmv);
            strftime(mod_str, sizeof(mod_str), "%b %e %H:%M", &tmv);
        }
        const char *dot = strrchr(m->full_path, '.');
        const char *ext = dot ? dot + 1 : "file";

        printf("\x1b[%d;1H\x1b[2K\x1b[2m  size     \x1b[0m%s", start + ln, size_str);
        ln++;
        printf("\x1b[%d;1H\x1b[2K\x1b[2m  modified \x1b[0m%s", start + ln, mod_str);
        ln++;
        printf("\x1b[%d;1H\x1b[2K\x1b[2m  type     \x1b[0m%s", start + ln, ext);
        ln++;
        char sep[256];
        int seplen = cols < 256 ? cols : 255;
        memset(sep, '-', (size_t)seplen);
        sep[seplen] = '\0';
        printf("\x1b[%d;1H\x1b[2K\x1b[2m%s\x1b[0m", start + ln, sep);
        ln++;

        FILE *f = fopen(m->full_path, "r");
        int total_lines = 0;
        if (f) {
            char linebuf[4096];
            char *file_lines[8192];
            int fl_count = 0;
            while (fgets(linebuf, sizeof(linebuf), f) && fl_count < 8192) {
                size_t l = strlen(linebuf);
                if (l > 0 && linebuf[l - 1] == '\n') {
                    linebuf[l - 1] = '\0';
                }
                file_lines[fl_count++] = strdup(linebuf);
            }
            fclose(f);
            total_lines = fl_count;

            int content_avail = avail - ln;
            if (content_avail < 0) {
                content_avail = 0;
            }
            if (preview_scroll > total_lines - content_avail) {
                preview_scroll = total_lines - content_avail;
            }
            if (preview_scroll < 0) {
                preview_scroll = 0;
            }

            for (int i = 0; i < content_avail && preview_scroll + i < fl_count; i++) {
                const char *lc = file_lines[preview_scroll + i];
                char trimmed[512];
                if ((int)strlen(lc) > cols - 7) {
                    snprintf(trimmed, sizeof(trimmed), "%.*s\xe2\x80\xa6", cols - 8, lc);
                } else {
                    snprintf(trimmed, sizeof(trimmed), "%s", lc);
                }
                printf("\x1b[%d;1H\x1b[2K\x1b[2m%4d \x1b[0m%s", start + ln, preview_scroll + i + 1, trimmed);
                ln++;
            }
            for (int i = 0; i < fl_count; i++) {
                free(file_lines[i]);
            }
        } else {
            printf("\x1b[%d;1H\x1b[2K\x1b[2m  (cannot read file)\x1b[0m", start + ln);
            ln++;
        }

        for (int i = ln; i < avail; i++) {
            printf("\x1b[%d;1H\x1b[2K", start + i);
        }

        int info_row = rows_count - 2;
        int prompt_row = rows_count - 1;
        if (!in_editor_picker) {
            printf("\x1b[%d;1H\x1b[2K  file: \x1b[1m%s\x1b[0m", info_row, m->display);
            printf("\x1b[%d;1H\x1b[2K  Press [\x1b[34;1mEnter\x1b[0m] to open editor...", prompt_row);
        } else {
            printf("\x1b[%d;1H\x1b[2K\x1b[33;1m  Choose your editor to open %s:\x1b[0m", info_row, m->display);
            printf("\x1b[%d;1H\x1b[2K  ", prompt_row);
            for (size_t i = 0; i < editor_count; i++) {
                if (i == e_sel) {
                    printf("\x1b[47;30;1m %s \x1b[0m ", installed[i]);
                } else {
                    printf("\x1b[36m %s \x1b[0m ", installed[i]);
                }
            }
        }
        tui_draw_bottom_bar("", "");
        fflush(stdout);

        int c = read_raw_byte();
        if (c < 0) {
            continue;
        }

        if (!in_editor_picker && (c == 'd' || c == 'D')) {
            show_delete_confirm(m);
            *opened = 0;
            return;
        }

        if (c == 0x1b || c == 3) {
            int next = -1;
            if (c == 0x1b) {
                next = tui_read_byte_after_esc();
            }
            if (next == '[') {
                int c3 = read_raw_byte();
                if (c3 == 'A') {
                    if (preview_scroll > 0) {
                        preview_scroll--;
                    }
                    continue;
                }
                if (c3 == 'B') {
                    if (preview_scroll + (tui_rows() - 10) < total_lines) {
                        preview_scroll++;
                    }
                    continue;
                }
                continue;
            }
            if (in_editor_picker) {
                in_editor_picker = 0;
                continue;
            }
            preview_scroll = 0;
            *opened = 0;
            return;
        }

        if (c == '\r' || c == '\n') {
            if (!in_editor_picker) {
                in_editor_picker = 1;
            } else {
                char dirbuf[4096];
                snprintf(dirbuf, sizeof(dirbuf), "%s", m->full_path);
                char *slash = strrchr(dirbuf, '/');
                if (slash) {
                    *slash = '\0';
                    if (chdir(dirbuf) != 0) {
                        /* best effort */
                    }
                }
                snprintf(chosen_cmd_out, chosen_out_size, "%s \"%s\"", installed[e_sel], m->full_path);
                *opened = 1;
                return;
            }
            continue;
        }

        if (in_editor_picker) {
            if (c == 0x1b) {
                continue;
            }
        }
    }
}

SearchSessionResult search_show(void) {
    SearchSessionResult result;
    memset(&result, 0, sizeof(result));

    char query[512] = {0};
    size_t cursor_pos = 0;
    size_t sel = 0;
    size_t scroll = 0;

    Grouped grouped;
    memset(&grouped, 0, sizeof(grouped));
    run_search(query, &grouped);
    size_t row_count;
    SrRow *rows = build_rows(&grouped, &row_count);
    for (size_t i = 0; i < row_count; i++) {
        if (rows[i].kind == ROW_RESULT) {
            sel = i;
            break;
        }
    }

    platform_raw_mode_enable();
    tui_enter_alt();

    for (;;) {
        adjust_scroll(sel, &scroll);
        render(&grouped, rows, row_count, sel, scroll, query, cursor_pos);

        int c = read_raw_byte();
        if (c < 0) {
            continue;
        }

        if (c == 0x1b) {
            int c2 = tui_read_byte_after_esc();
            if (c2 != '[') {
                break;
            }
            int c3 = read_raw_byte();
            if (c3 == 'A') {
                if (row_count > 0) {
                    long next = (long)sel - 1;
                    while (next >= 0 && rows[next].kind == ROW_HEADER) {
                        next--;
                    }
                    if (next >= 0) {
                        sel = (size_t)next;
                    }
                }
            } else if (c3 == 'B') {
                if (row_count > 0) {
                    size_t next = sel + 1;
                    while (next < row_count && rows[next].kind == ROW_HEADER) {
                        next++;
                    }
                    if (next < row_count) {
                        sel = next;
                    }
                }
            } else if (c3 == 'D') {
                if (cursor_pos > 0) {
                    cursor_pos--;
                }
            } else if (c3 == 'C') {
                if (cursor_pos < strlen(query)) {
                    cursor_pos++;
                }
            }
            continue;
        }

        if (c == 3) {
            break;
        }

        if (c == '\r' || c == '\n') {
            if (row_count == 0 || rows[sel].kind != ROW_RESULT) {
                continue;
            }
            const SearchMatch *m = &grouped.buckets[rows[sel].cat].items[rows[sel].match_idx];

            if (m->kind == RES_HISTORY || m->kind == RES_BUILTIN || m->kind == RES_ALIAS ||
                m->kind == RES_EXECUTABLE) {
                snprintf(result.value, sizeof(result.value), "%s", m->value);
                result.has_result = 1;
                goto done;
            }
            if (m->kind == RES_DIR) {
                int do_cd = 0;
                show_dir_action(m, &do_cd);
                if (do_cd) {
                    if (chdir(m->full_path) != 0) {
                        /* best effort */
                    }
                    result.has_result = 1;
                    result.did_cd = 1;
                    result.value[0] = '\0';
                    goto done;
                }
                run_search(query, &grouped);
                free(rows);
                rows = build_rows(&grouped, &row_count);
                continue;
            }
            if (m->kind == RES_FILE) {
                int opened = 0;
                char chosen[8192];
                show_file_action(m, chosen, sizeof(chosen), &opened);
                if (opened) {
                    snprintf(result.value, sizeof(result.value), "%s", chosen);
                    result.has_result = 1;
                    goto done;
                }
                run_search(query, &grouped);
                free(rows);
                rows = build_rows(&grouped, &row_count);
                continue;
            }
            continue;
        }

        if (c == 127) {
            if (cursor_pos > 0) {
                size_t len = strlen(query);
                memmove(query + cursor_pos - 1, query + cursor_pos, len - cursor_pos + 1);
                cursor_pos--;
                run_search(query, &grouped);
                free(rows);
                rows = build_rows(&grouped, &row_count);
                sel = 0;
                for (size_t i = 0; i < row_count; i++) {
                    if (rows[i].kind == ROW_RESULT) {
                        sel = i;
                        break;
                    }
                }
            }
            continue;
        }

        if (c >= 32 && c < 127) {
            size_t len = strlen(query);
            if (len + 1 < sizeof(query)) {
                memmove(query + cursor_pos + 1, query + cursor_pos, len - cursor_pos + 1);
                query[cursor_pos] = (char)c;
                cursor_pos++;
                run_search(query, &grouped);
                free(rows);
                rows = build_rows(&grouped, &row_count);
                sel = 0;
                for (size_t i = 0; i < row_count; i++) {
                    if (rows[i].kind == ROW_RESULT) {
                        sel = i;
                        break;
                    }
                }
            }
            continue;
        }
    }

done:
    tui_exit_alt();
    platform_raw_mode_disable();
    free(rows);
    grouped_free(&grouped);
    return result;
}
