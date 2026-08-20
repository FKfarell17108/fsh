#include "completion/completion_picker.h"

#include "platform/platform.h"
#include "tui/tui.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int read_raw_byte(void) {
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) {
        return -1;
    }
    return (unsigned char)c;
}

static size_t max_len(const CandidateList *c) {
    size_t m = 0;
    for (size_t i = 0; i < c->count; i++) {
        size_t l = strlen(c->items[i]);
        if (l > m) {
            m = l;
        }
    }
    return m;
}

static int cell_w(const CandidateList *c) {
    int w = (int)max_len(c) + 2;
    return w > 16 ? w : 16;
}

static int per_row(const CandidateList *c) {
    int cw = cell_w(c);
    int p = tui_cols() / cw;
    return p > 1 ? p : 1;
}

static int total_rows(const CandidateList *c) {
    int p = per_row(c);
    return (int)((c->count + (size_t)p - 1) / (size_t)p);
}

static int vis_rows(void) {
    int r = tui_rows() - 2 - 2;
    return r > 1 ? r : 1;
}

static void build_left(const CandidateList *c, char *out, size_t out_size) {
    if (c->count == 0) {
        snprintf(out, out_size, "No matches");
        return;
    }
    size_t dirs = 0;
    for (size_t i = 0; i < c->count; i++) {
        size_t l = strlen(c->items[i]);
        if (l > 0 && c->items[i][l - 1] == '/') {
            dirs++;
        }
    }
    size_t files = c->count - dirs;
    char parts[128] = {0};
    if (dirs > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%zud", dirs);
        strcat(parts, buf);
    }
    if (files > 0) {
        char buf[32];
        if (parts[0]) {
            strcat(parts, "  ");
        }
        snprintf(buf, sizeof(buf), "%zu item%s", files, files == 1 ? "" : "s");
        strcat(parts, buf);
    }
    if (!parts[0]) {
        snprintf(parts, sizeof(parts), "%zu items", c->count);
    }
    snprintf(out, out_size, "%s", parts);
}

static void draw(const CandidateList *c, size_t sel_idx, int scroll_top) {
    tui_clear_screen();

    NavRows nr;
    navrows_init(&nr);
    size_t r0 = navrows_add_row(&nr);
    navrows_add_item(&nr, r0, "Nav", "Navigate", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "Ent", "Select", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "Esc", "Cancel", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "Tab", "History", NAV_CYAN);
    tui_draw_navbar(&nr);
    navrows_free(&nr);

    int start = 4;
    int v = vis_rows();
    int p = per_row(c);
    int cw = cell_w(c);

    if (c->count == 0) {
        printf("\x1b[%d;1H\x1b[2K\x1b[2m  (no matches)\x1b[0m", start);
        for (int i = 1; i < v; i++) {
            printf("\x1b[%d;1H\x1b[2K", start + i);
        }
    } else {
        for (int row = 0; row < v; row++) {
            printf("\x1b[%d;1H\x1b[2K ", start + row);
            int fr = scroll_top + row;
            for (int col = 0; col < p; col++) {
                size_t i = (size_t)(fr * p + col);
                if (i >= c->count) {
                    break;
                }
                const char *name = c->items[i];
                size_t nlen = strlen(name);
                int is_dir = nlen > 0 && name[nlen - 1] == '/';
                int is_hidden = name[0] == '.';
                int is_sel = (i == sel_idx);

                char padded[2048];
                int plen = snprintf(padded, sizeof(padded), "%s", name);
                for (int k = plen; k < cw && (size_t)k + 1 < sizeof(padded); k++) {
                    padded[k] = ' ';
                }
                padded[cw < (int)sizeof(padded) - 1 ? cw : (int)sizeof(padded) - 1] = '\0';

                if (is_sel) {
                    printf("\x1b[47;30;1m%s\x1b[0m", padded);
                } else if (is_dir && is_hidden) {
                    printf("\x1b[36m%s\x1b[0m", padded);
                } else if (is_dir) {
                    printf("\x1b[34;1m%s\x1b[0m", padded);
                } else if (is_hidden) {
                    printf("\x1b[90m%s\x1b[0m", padded);
                } else {
                    printf("%s", padded);
                }
            }
        }
    }

    char left[128];
    build_left(c, left, sizeof(left));
    tui_draw_footer(total_rows(c), scroll_top, v, left);
    fflush(stdout);
}

CompletionPickResult completion_picker_show(const CandidateList *candidates) {
    CompletionPickResult result;
    memset(&result, 0, sizeof(result));

    if (candidates->count == 0) {
        result.kind = COMPLETION_PICK_NONE;
        return result;
    }

    size_t sel_idx = 0;
    int scroll_top = 0;
    int p = per_row(candidates);

    platform_raw_mode_enable();
    tui_enter_alt();

    for (;;) {
        int row = (int)sel_idx / p;
        int v = vis_rows();
        if (row < scroll_top) {
            scroll_top = row;
        }
        if (row >= scroll_top + v) {
            scroll_top = row - v + 1;
        }

        draw(candidates, sel_idx, scroll_top);

        int c = read_raw_byte();
        if (c < 0) {
            continue;
        }

        if (c == 3 || c == 0x1b) {
            int next = -1;
            if (c == 0x1b) {
                next = tui_read_byte_after_esc();
            }
            if (next != '[') {
                tui_exit_alt();
                platform_raw_mode_disable();
                result.kind = COMPLETION_PICK_NONE;
                return result;
            }
            int c3 = read_raw_byte();
            int idx = (int)sel_idx;
            if (c3 == 'A') idx -= p;
            else if (c3 == 'B') idx += p;
            else if (c3 == 'C') idx += 1;
            else if (c3 == 'D') idx -= 1;
            else if (c3 == 'H') idx = 0;
            else if (c3 == 'F') idx = (int)candidates->count - 1;
            if (idx < 0) idx = 0;
            if (idx > (int)candidates->count - 1) idx = (int)candidates->count - 1;
            sel_idx = (size_t)idx;
            continue;
        }

        if (c == '\t') {
            tui_exit_alt();
            platform_raw_mode_disable();
            result.kind = COMPLETION_PICK_HISTORY;
            return result;
        }

        if (c == '\r' || c == '\n') {
            tui_exit_alt();
            platform_raw_mode_disable();
            result.kind = COMPLETION_PICK_SELECTED;
            snprintf(result.chosen, sizeof(result.chosen), "%s", candidates->items[sel_idx]);
            return result;
        }
    }
}
