#include "browser/interactive_browser.h"

#include "bookmarks/bookmarks.h"
#include "fileops/file_ops.h"
#include "fileops/trash.h"
#include "platform/platform.h"
#include "preview/preview.h"
#include "prompt/git_info.h"
#include "sort/sort.h"
#include "tui/tui.h"
#include "util/strbuf.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct {
    char name[1024];
    int is_dir;
    int hidden;
    long long size;
    long long mtime_ms;
} BrowserEntry;

typedef struct {
    char cwd[4096];
    BrowseMode mode;
    LsSort sort;
    int show_hidden;
    BrowserEntry *entries;
    size_t entry_count;
    size_t sel_index;
    int *selected;
    size_t select_count;
    PreviewPref preview_pref;
    int preview_visible;
    PreviewState preview;
    char search_query[256];
    int search_active;
    char status_msg[256];
    long long status_until;
    GitFileEntry *git_entries;
    size_t git_entry_count;
    int num_cols;
    int num_rows;
} BrowserState;

static long long now_millis(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void set_status(BrowserState *st, const char *msg) {
    snprintf(st->status_msg, sizeof(st->status_msg), "%s", msg);
    st->status_until = now_millis() + 2500;
}

static int fuzzy_match(const char *query, const char *target) {
    if (query[0] == '\0') {
        return 1;
    }
    size_t qi = 0;
    size_t ti = 0;
    size_t qlen = strlen(query);
    size_t tlen = strlen(target);
    while (qi < qlen && ti < tlen) {
        if (tolower((unsigned char)query[qi]) == tolower((unsigned char)target[ti])) {
            qi++;
        }
        ti++;
    }
    return qi == qlen;
}

static void free_entries(BrowserState *st) {
    free(st->entries);
    st->entries = NULL;
    st->entry_count = 0;
    free(st->selected);
    st->selected = NULL;
    free(st->git_entries);
    st->git_entries = NULL;
    st->git_entry_count = 0;
}

static void load_entries(BrowserState *st) {
    free_entries(st);

    DIR *dp = opendir(st->cwd);
    if (!dp) {
        return;
    }

    BrowserEntry *entries = NULL;
    size_t count = 0;
    size_t capacity = 0;

    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        int hidden = entry->d_name[0] == '.';
        if (hidden && !st->show_hidden) {
            continue;
        }
        if (st->search_active && !fuzzy_match(st->search_query, entry->d_name)) {
            continue;
        }

        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", st->cwd, entry->d_name);
        struct stat s;
        int is_dir = 0;
        long long size = 0;
        long long mtime = 0;
        if (lstat(full, &s) == 0) {
            is_dir = S_ISDIR(s.st_mode);
            size = s.st_size;
            mtime = (long long)s.st_mtime * 1000;
        }

        if (st->mode == BROWSE_MODE_DIR && !is_dir) {
            continue;
        }

        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 64;
            entries = realloc(entries, capacity * sizeof(BrowserEntry));
        }
        snprintf(entries[count].name, sizeof(entries[count].name), "%s", entry->d_name);
        entries[count].is_dir = is_dir;
        entries[count].hidden = hidden;
        entries[count].size = size;
        entries[count].mtime_ms = mtime;
        count++;
    }
    closedir(dp);

    SortableEntry *sortables = malloc((count > 0 ? count : 1) * sizeof(SortableEntry));
    for (size_t i = 0; i < count; i++) {
        snprintf(sortables[i].name, sizeof(sortables[i].name), "%s", entries[i].name);
        sortables[i].is_dir = entries[i].is_dir;
        sortables[i].hidden = entries[i].hidden;
        sortables[i].size = entries[i].size;
        sortables[i].mtime_ms = entries[i].mtime_ms;
    }
    sort_ls_entries(sortables, count, st->sort);

    BrowserEntry *sorted = malloc((count > 0 ? count : 1) * sizeof(BrowserEntry));
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < count; j++) {
            if (strcmp(entries[j].name, sortables[i].name) == 0) {
                sorted[i] = entries[j];
                break;
            }
        }
    }
    free(entries);
    free(sortables);

    st->entries = sorted;
    st->entry_count = count;
    st->selected = calloc(count > 0 ? count : 1, sizeof(int));
    st->select_count = 0;
    if (st->sel_index >= count && count > 0) {
        st->sel_index = count - 1;
    }
    if (count == 0) {
        st->sel_index = 0;
    }

    st->git_entries = git_file_statuses(st->cwd, &st->git_entry_count);
}

static const GitFileEntry *git_lookup(BrowserState *st, const char *name) {
    for (size_t i = 0; i < st->git_entry_count; i++) {
        if (strcmp(st->git_entries[i].name, name) == 0) {
            return &st->git_entries[i];
        }
    }
    return NULL;
}

static void compute_grid(BrowserState *st, int list_w) {
    size_t max_name = 4;
    for (size_t i = 0; i < st->entry_count; i++) {
        size_t l = strlen(st->entries[i].name) + (st->entries[i].is_dir ? 1 : 0);
        if (l > max_name) {
            max_name = l;
        }
    }
    int col_w = (int)max_name + 4;
    int cols = list_w / col_w;
    if (cols < 1) {
        cols = 1;
    }
    if (st->entry_count > 0 && (size_t)cols > st->entry_count) {
        cols = (int)st->entry_count;
    }
    st->num_cols = cols;
    st->num_rows = cols > 0 ? (int)((st->entry_count + (size_t)cols - 1) / (size_t)cols) : 0;
}

static void full_path_of(BrowserState *st, size_t idx, char *out, size_t out_size) {
    snprintf(out, out_size, "%s/%s", st->cwd, st->entries[idx].name);
}

static const char *git_badge_for(BrowserState *st, const BrowserEntry *e) {
    const GitFileEntry *g = git_lookup(st, e->name);
    return g ? git_status_badge(g->status) : NULL;
}

static void draw(BrowserState *st) {
    tui_clear_screen();

    NavRows nr;
    navrows_init(&nr);
    size_t r0 = navrows_add_row(&nr);
    navrows_add_item(&nr, r0, "Nav", "Navigate", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "Spc", "Select", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "A", "All", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "C", "Copy", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "X", "Cut", NAV_YELLOW);
    navrows_add_item(&nr, r0, "V", "Paste", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "R", "Rename", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "D", "Delete", NAV_RED);
    size_t r1 = navrows_add_row(&nr);
    navrows_add_item(&nr, r1, "N", "New folder", NAV_GREEN);
    navrows_add_item(&nr, r1, "T", "New file", NAV_GREEN);
    navrows_add_item(&nr, r1, "S", "Sort", NAV_DEFAULT);
    navrows_add_item(&nr, r1, "B", "Bookmark", NAV_CYAN);
    navrows_add_item(&nr, r1, ".", "Hidden", NAV_DEFAULT);
    navrows_add_item(&nr, r1, "/", "Search", NAV_DEFAULT);
    navrows_add_item(&nr, r1, "P", "Preview", NAV_DEFAULT);
    navrows_add_item(&nr, r1, "Esc", "Quit", NAV_DEFAULT);
    tui_draw_navbar(&nr);
    navrows_free(&nr);

    int nav_rows = 2;
    PreviewMode pmode = preview_get_mode(st->preview_pref);
    int cols_total = tui_cols();
    int list_w = st->preview_visible && pmode == PREVIEW_MODE_SPLIT ? preview_list_cols() : cols_total;

    compute_grid(st, list_w);

    int start_r = nav_rows + 2;
    int end_r = tui_rows() - 1;
    int vis_rows_count = end_r - start_r + 1;
    if (vis_rows_count < 1) {
        vis_rows_count = 1;
    }

    int col_w = st->num_cols > 0 ? list_w / st->num_cols : list_w;

    int sel_row = (st->num_cols > 0 && st->num_rows > 0) ? (int)(st->sel_index) % st->num_rows : 0;

    int row_scroll = 0;
    if (sel_row >= vis_rows_count) {
        row_scroll = sel_row - vis_rows_count + 1;
    }

    if (st->entry_count == 0) {
        printf("\x1b[%d;1H\x1b[2K\x1b[2m  (empty)\x1b[0m", start_r);
        for (int i = 1; i < vis_rows_count; i++) {
            printf("\x1b[%d;1H\x1b[2K", start_r + i);
        }
    } else {
        for (int r = 0; r < vis_rows_count; r++) {
            printf("\x1b[%d;1H\x1b[2K", start_r + r);
            int actual_row = r + row_scroll;
            if (actual_row >= st->num_rows) {
                continue;
            }
            StrBuf line;
            strbuf_init(&line);
            for (int c = 0; c < st->num_cols; c++) {
                size_t idx = (size_t)(c * st->num_rows + actual_row);
                if (idx >= st->entry_count) {
                    for (int k = 0; k < col_w; k++) {
                        strbuf_push_char(&line, ' ');
                    }
                    continue;
                }
                const BrowserEntry *e = &st->entries[idx];
                int is_sel = (idx == st->sel_index);
                int is_marked = st->selected[idx];

                char cell[1200];
                const char *badge = git_badge_for(st, e);
                snprintf(cell, sizeof(cell), "%s%s%s%s%s", is_marked ? "\xe2\x9c\x93 " : "  ", e->name,
                         e->is_dir ? "/" : "", badge ? " " : "", badge ? badge : "");

                char *padded = tui_pad_or_trim(cell, col_w - 1);
                if (is_sel) {
                    strbuf_push_str(&line, "\x1b[47;30;1m");
                    strbuf_push_str(&line, padded);
                    strbuf_push_str(&line, "\x1b[0m ");
                } else if (e->is_dir) {
                    strbuf_push_str(&line, "\x1b[38;2;107;191;255;1m");
                    strbuf_push_str(&line, padded);
                    strbuf_push_str(&line, "\x1b[0m ");
                } else if (e->hidden) {
                    strbuf_push_str(&line, "\x1b[2m");
                    strbuf_push_str(&line, padded);
                    strbuf_push_str(&line, "\x1b[0m ");
                } else {
                    strbuf_push_str(&line, padded);
                    strbuf_push_char(&line, ' ');
                }
                free(padded);
            }
            fputs(line.data, stdout);
            strbuf_free(&line);
        }
    }

    if (st->preview_visible) {
        if (st->entry_count > 0) {
            char full[4096];
            full_path_of(st, st->sel_index, full, sizeof(full));
            preview_update(&st->preview, full);
        }
        if (pmode == PREVIEW_MODE_SPLIT) {
            preview_draw_split(&st->preview, nav_rows, list_w);
        } else {
            preview_draw_overlay(&st->preview, nav_rows);
        }
    }

    char left[512];
    const char *msg = (now_millis() < st->status_until) ? st->status_msg : NULL;
    char cwd_disp[4096];
    const char *home = getenv("HOME");
    if (home && strncmp(st->cwd, home, strlen(home)) == 0) {
        snprintf(cwd_disp, sizeof(cwd_disp), "~%s", st->cwd + strlen(home));
    } else {
        snprintf(cwd_disp, sizeof(cwd_disp), "%s", st->cwd);
    }
    if (msg) {
        snprintf(left, sizeof(left), "%s  \xe2\x80\x94  %s", cwd_disp, msg);
    } else if (st->search_active) {
        snprintf(left, sizeof(left), "%s  search: %s", cwd_disp, st->search_query);
    } else {
        snprintf(left, sizeof(left), "%s  %zu item%s%s", cwd_disp, st->entry_count,
                 st->entry_count == 1 ? "" : "s", st->show_hidden ? "  [hidden shown]" : "");
    }
    tui_draw_footer((int)st->entry_count, 0, (int)st->entry_count, left);

    fflush(stdout);
}

static int read_raw_byte(void) {
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) {
        return -1;
    }
    return (unsigned char)c;
}

static void move_selection(BrowserState *st, int d_row, int d_col) {
    if (st->entry_count == 0 || st->num_rows == 0 || st->num_cols == 0) {
        return;
    }
    int row = (int)(st->sel_index) % st->num_rows;
    int col = (int)(st->sel_index) / st->num_rows;

    row += d_row;
    col += d_col;

    if (row < 0) {
        row = st->num_rows - 1;
    }
    if (row >= st->num_rows) {
        row = 0;
    }
    if (col < 0) {
        col = st->num_cols - 1;
    }
    if (col >= st->num_cols) {
        col = 0;
    }

    size_t idx = (size_t)(col * st->num_rows + row);
    if (idx >= st->entry_count) {
        idx = st->entry_count - 1;
    }
    st->sel_index = idx;
}

static void toggle_select_current(BrowserState *st) {
    if (st->entry_count == 0) {
        return;
    }
    if (st->selected[st->sel_index]) {
        st->selected[st->sel_index] = 0;
        st->select_count--;
    } else {
        st->selected[st->sel_index] = 1;
        st->select_count++;
    }
}

static size_t collect_selected(BrowserState *st, size_t **out_indices) {
    size_t count = 0;
    for (size_t i = 0; i < st->entry_count; i++) {
        if (st->selected[i]) {
            count++;
        }
    }
    if (count == 0 && st->entry_count > 0) {
        *out_indices = malloc(sizeof(size_t));
        (*out_indices)[0] = st->sel_index;
        return 1;
    }
    *out_indices = malloc((count > 0 ? count : 1) * sizeof(size_t));
    size_t j = 0;
    for (size_t i = 0; i < st->entry_count; i++) {
        if (st->selected[i]) {
            (*out_indices)[j++] = i;
        }
    }
    return count;
}

static void clear_selection(BrowserState *st) {
    for (size_t i = 0; i < st->entry_count; i++) {
        st->selected[i] = 0;
    }
    st->select_count = 0;
}

static int is_video_ext(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return 0;
    }
    static const char *exts[] = {"mp4", "mkv", "avi", "mov", "webm", "flv", "wmv"};
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        if (strcasecmp(dot + 1, exts[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_image_ext_name(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return 0;
    }
    static const char *exts[] = {"png", "jpg", "jpeg", "gif", "bmp", "webp", "ico"};
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        if (strcasecmp(dot + 1, exts[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static void pick_editor(char *out, size_t out_size) {
    const char *env_editor = getenv("EDITOR");
    if (env_editor && env_editor[0] != '\0') {
        snprintf(out, out_size, "%s", env_editor);
        return;
    }
    const char *candidates[] = {"nano", "vim", "vi"};
    const char *path_env = getenv("PATH");
    if (path_env) {
        for (size_t c = 0; c < sizeof(candidates) / sizeof(candidates[0]); c++) {
            char *copy = strdup(path_env);
            char *saveptr = NULL;
            char *dir = strtok_r(copy, ":", &saveptr);
            int found = 0;
            while (dir) {
                char full[4096];
                snprintf(full, sizeof(full), "%s/%s", dir, candidates[c]);
                if (access(full, X_OK) == 0) {
                    found = 1;
                    break;
                }
                dir = strtok_r(NULL, ":", &saveptr);
            }
            free(copy);
            if (found) {
                snprintf(out, out_size, "%s", candidates[c]);
                return;
            }
        }
    }
    snprintf(out, out_size, "vi");
}

static void open_in_editor(const char *full_path) {
    char editor[256];
    pick_editor(editor, sizeof(editor));

    Command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = editor;
    char *args[1];
    args[0] = (char *)full_path;
    cmd.args = args;
    cmd.argc = 1;

    tui_exit_alt();
    int exit_code = 0;
    platform_spawn_pty(&cmd, &exit_code);
    tui_enter_alt();
}

BrowserResult interactive_browser_run(BrowseMode mode) {
    BrowserState st;
    memset(&st, 0, sizeof(st));
    st.mode = mode;
    st.sort = DEFAULT_LS_SORT;
    st.preview_pref = PREVIEW_PREF_AUTO;
    preview_state_init(&st.preview);
    bookmarks_load();

    if (!getcwd(st.cwd, sizeof(st.cwd))) {
        snprintf(st.cwd, sizeof(st.cwd), "/");
    }
    char start_cwd[4096];
    snprintf(start_cwd, sizeof(start_cwd), "%s", st.cwd);

    load_entries(&st);

    platform_raw_mode_enable();
    tui_enter_alt();

    BrowserResult result;
    memset(&result, 0, sizeof(result));
    result.kind = BROWSER_RESULT_QUIT;

    for (;;) {
        draw(&st);

        int c = read_raw_byte();
        if (c < 0) {
            continue;
        }

        if (st.search_active) {
            if (c == 0x1b) {
                int c2 = tui_read_byte_after_esc();
                if (c2 != '[') {
                    st.search_active = 0;
                    st.search_query[0] = '\0';
                    load_entries(&st);
                    continue;
                }
                int c3 = read_raw_byte();
                if (c3 == 'A') move_selection(&st, -1, 0);
                else if (c3 == 'B') move_selection(&st, 1, 0);
                else if (c3 == 'C') move_selection(&st, 0, 1);
                else if (c3 == 'D') move_selection(&st, 0, -1);
                continue;
            }
            if (c == '\r' || c == '\n') {
                st.search_active = 0;
                continue;
            }
            if (c == 127 || c == 8) {
                size_t len = strlen(st.search_query);
                if (len > 0) {
                    st.search_query[len - 1] = '\0';
                    load_entries(&st);
                }
                continue;
            }
            if (c >= 32 && c < 127) {
                size_t len = strlen(st.search_query);
                if (len + 1 < sizeof(st.search_query)) {
                    st.search_query[len] = (char)c;
                    st.search_query[len + 1] = '\0';
                    load_entries(&st);
                }
                continue;
            }
            continue;
        }

        if (c == 3 || c == 0x1b) {
            int next = -1;
            if (c == 0x1b) {
                next = tui_read_byte_after_esc();
            }
            if (next != '[') {
                free_entries(&st);
                tui_exit_alt();
                platform_raw_mode_disable();
                if (strcmp(st.cwd, start_cwd) != 0) {
                    result.kind = BROWSER_RESULT_CD;
                    snprintf(result.path, sizeof(result.path), "%s", st.cwd);
                } else {
                    result.kind = BROWSER_RESULT_QUIT;
                }
                return result;
            }
            int c3 = read_raw_byte();
            if (c3 == 'A') move_selection(&st, -1, 0);
            else if (c3 == 'B') move_selection(&st, 1, 0);
            else if (c3 == 'C') move_selection(&st, 0, 1);
            else if (c3 == 'D') move_selection(&st, 0, -1);
            continue;
        }

        if (c == '\t') {
            char parent[4096];
            snprintf(parent, sizeof(parent), "%s", st.cwd);
            char *slash = strrchr(parent, '/');
            if (slash && slash != parent) {
                *slash = '\0';
            } else if (slash == parent) {
                parent[1] = '\0';
            }
            snprintf(st.cwd, sizeof(st.cwd), "%s", parent);
            st.sel_index = 0;
            load_entries(&st);
            continue;
        }

        if (c == '\r' || c == '\n') {
            if (st.entry_count == 0) {
                continue;
            }
            const BrowserEntry *e = &st.entries[st.sel_index];
            char full[4096];
            full_path_of(&st, st.sel_index, full, sizeof(full));

            if (e->is_dir) {
                snprintf(st.cwd, sizeof(st.cwd), "%s", full);
                st.sel_index = 0;
                load_entries(&st);
            } else if (is_image_ext_name(e->name)) {
                set_status(&st, "Image preview belum didukung");
            } else if (is_video_ext(e->name)) {
                set_status(&st, "Video preview belum didukung");
            } else {
                open_in_editor(full);
            }
            continue;
        }

        if (c == ' ') {
            toggle_select_current(&st);
            move_selection(&st, 1, 0);
            continue;
        }

        if (c == 'a' || c == 'A') {
            if (st.select_count == st.entry_count) {
                clear_selection(&st);
            } else {
                for (size_t i = 0; i < st.entry_count; i++) {
                    st.selected[i] = 1;
                }
                st.select_count = st.entry_count;
            }
            continue;
        }

        if (c == '.') {
            st.show_hidden = !st.show_hidden;
            load_entries(&st);
            continue;
        }

        if (c == '/') {
            st.search_active = 1;
            st.search_query[0] = '\0';
            continue;
        }

        if (c == 'p' || c == 'P') {
            st.preview_visible = !st.preview_visible;
            continue;
        }

        if (c == 's' || c == 'S') {
            SortPickResult r = sort_show_picker("ls", st.sort.key, st.sort.dir, tui_rows() - 2);
            if (r.picked) {
                st.sort.key = (LsSortKey)r.key;
                st.sort.dir = r.dir;
                load_entries(&st);
            }
            continue;
        }

        if (c == 'n' || c == 'N') {
            TuiPopupResult r = tui_popup_input("New folder", "", 1, NULL, 0, NULL, NULL);
            if (r.confirmed && r.value[0] != '\0') {
                char full[4096];
                snprintf(full, sizeof(full), "%s/%s", st.cwd, r.value);
                if (mkdir(full, 0755) == 0) {
                    set_status(&st, "Folder created");
                    load_entries(&st);
                } else {
                    set_status(&st, "Could not create folder");
                }
            }
            continue;
        }

        if (c == 't' || c == 'T') {
            TuiPopupResult r = tui_popup_input("New file", "", 1, NULL, 0, NULL, NULL);
            if (r.confirmed && r.value[0] != '\0') {
                char full[4096];
                snprintf(full, sizeof(full), "%s/%s", st.cwd, r.value);
                FILE *f = fopen(full, "a");
                if (f) {
                    fclose(f);
                    set_status(&st, "File created");
                    load_entries(&st);
                } else {
                    set_status(&st, "Could not create file");
                }
            }
            continue;
        }

        if (c == 'r' || c == 'R') {
            if (st.entry_count == 0) {
                continue;
            }
            const BrowserEntry *e = &st.entries[st.sel_index];
            TuiPopupResult r = tui_popup_input("Rename", e->name, 1, NULL, 0, NULL, NULL);
            if (r.confirmed && r.value[0] != '\0') {
                char full[4096];
                full_path_of(&st, st.sel_index, full, sizeof(full));
                char *err = file_ops_exec_rename(full, r.value);
                if (err) {
                    set_status(&st, err);
                    free(err);
                } else {
                    set_status(&st, "Renamed");
                    load_entries(&st);
                }
            }
            continue;
        }

        if (c == 'c' || c == 'C') {
            size_t *indices;
            size_t n = collect_selected(&st, &indices);
            ClipboardEntry *items = malloc((n > 0 ? n : 1) * sizeof(ClipboardEntry));
            for (size_t i = 0; i < n; i++) {
                const BrowserEntry *e = &st.entries[indices[i]];
                full_path_of(&st, indices[i], items[i].src_path, sizeof(items[i].src_path));
                snprintf(items[i].src_name, sizeof(items[i].src_name), "%s", e->name);
                items[i].is_dir = e->is_dir;
            }
            file_ops_set_clipboard(0, items, n);
            free(items);
            free(indices);
            char msg[64];
            snprintf(msg, sizeof(msg), "%zu item%s copied", n, n == 1 ? "" : "s");
            set_status(&st, msg);
            continue;
        }

        if (c == 'x' || c == 'X') {
            size_t *indices;
            size_t n = collect_selected(&st, &indices);
            ClipboardEntry *items = malloc((n > 0 ? n : 1) * sizeof(ClipboardEntry));
            for (size_t i = 0; i < n; i++) {
                const BrowserEntry *e = &st.entries[indices[i]];
                full_path_of(&st, indices[i], items[i].src_path, sizeof(items[i].src_path));
                snprintf(items[i].src_name, sizeof(items[i].src_name), "%s", e->name);
                items[i].is_dir = e->is_dir;
            }
            file_ops_set_clipboard(1, items, n);
            free(items);
            free(indices);
            char msg[64];
            snprintf(msg, sizeof(msg), "%zu item%s cut", n, n == 1 ? "" : "s");
            set_status(&st, msg);
            continue;
        }

        if (c == 'v') {
            const Clipboard *cb = file_ops_get_clipboard();
            if (!cb->active || cb->count == 0) {
                set_status(&st, "Clipboard is empty");
                continue;
            }
            int errors = 0;
            for (size_t i = 0; i < cb->count; i++) {
                char *dest = file_ops_unique_dest(st.cwd, cb->items[i].src_name);
                char *err = cb->is_cut ? file_ops_exec_move(cb->items[i].src_path, dest)
                                       : file_ops_exec_copy(cb->items[i].src_path, dest);
                if (err) {
                    errors++;
                    free(err);
                }
                free(dest);
            }
            if (cb->is_cut) {
                file_ops_clear_clipboard();
            }
            char msg[64];
            snprintf(msg, sizeof(msg), errors == 0 ? "Pasted" : "Pasted with %d error(s)", errors);
            set_status(&st, msg);
            load_entries(&st);
            continue;
        }

        if (c == 'b' || c == 'B') {
            if (st.entry_count == 0) {
                continue;
            }
            char full[4096];
            full_path_of(&st, st.sel_index, full, sizeof(full));
            const char *action = bookmarks_toggle(full);
            char msg[64];
            snprintf(msg, sizeof(msg), "Bookmark %s", action);
            set_status(&st, msg);
            continue;
        }

        if (c == 'd' || c == 'D') {
            size_t *indices;
            size_t n = collect_selected(&st, &indices);
            if (n == 0) {
                free(indices);
                continue;
            }
            char body0[256];
            if (n == 1) {
                snprintf(body0, sizeof(body0), "%s", st.entries[indices[0]].name);
            } else {
                snprintf(body0, sizeof(body0), "%zu items selected", n);
            }
            const char *body[] = {body0, "This will be moved to trash."};
            TuiPopupResult r = tui_popup_input("Delete?", NULL, 0, body, 2, NULL, NULL);
            if (r.confirmed) {
                int errors = 0;
                for (size_t i = 0; i < n; i++) {
                    char full[4096];
                    full_path_of(&st, indices[i], full, sizeof(full));
                    char *err = trash_move_to_trash(full, NULL);
                    if (err) {
                        errors++;
                        free(err);
                    }
                }
                char msg[64];
                snprintf(msg, sizeof(msg), errors == 0 ? "Moved to trash" : "Deleted with %d error(s)", errors);
                set_status(&st, msg);
                load_entries(&st);
            }
            free(indices);
            continue;
        }
    }
}

BrowserResult interactive_ls(void) {
    return interactive_browser_run(BROWSE_MODE_LS);
}

BrowserResult interactive_dir(void) {
    return interactive_browser_run(BROWSE_MODE_DIR);
}
