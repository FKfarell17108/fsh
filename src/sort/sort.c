#include "sort/sort.h"

#include "tui/tui.h"
#include "util/strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const LsSort DEFAULT_LS_SORT = {LS_SORT_TYPE, SORT_ASC};
const TrashSort DEFAULT_TRASH_SORT = {TRASH_SORT_DATE, SORT_DESC};
const LogSort DEFAULT_LOG_SORT = {LOG_SORT_DATE, SORT_DESC};

static LsSort g_current_sort;

static int cmp_ll(long long a, long long b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

static int cmp_ls(const void *ap, const void *bp) {
    const SortableEntry *a = ap;
    const SortableEntry *b = bp;
    LsSort s = g_current_sort;

    if (s.key == LS_SORT_HIDDEN) {
        int ah = a->hidden ? 1 : 0;
        int bh = b->hidden ? 1 : 0;
        if (ah != bh) {
            return ah - bh;
        }
        int dircmp = (b->is_dir ? 1 : 0) - (a->is_dir ? 1 : 0);
        if (dircmp != 0) {
            return dircmp;
        }
        return strcmp(a->name, b->name);
    }
    if (s.key == LS_SORT_TYPE) {
        int cmp = (b->is_dir ? 1 : 0) - (a->is_dir ? 1 : 0);
        if (cmp != 0) {
            return s.dir == SORT_ASC ? cmp : -cmp;
        }
        return strcmp(a->name, b->name);
    }
    if (s.key == LS_SORT_NAME) {
        int cmp = strcmp(a->name, b->name);
        return s.dir == SORT_ASC ? cmp : -cmp;
    }
    if (s.key == LS_SORT_SIZE) {
        return s.dir == SORT_ASC ? cmp_ll(a->size, b->size) : cmp_ll(b->size, a->size);
    }
    if (s.key == LS_SORT_DATE) {
        return s.dir == SORT_DESC ? cmp_ll(b->mtime_ms, a->mtime_ms) : cmp_ll(a->mtime_ms, b->mtime_ms);
    }
    return 0;
}

void sort_ls_entries(SortableEntry *entries, size_t count, LsSort sort) {
    g_current_sort = sort;
    qsort(entries, count, sizeof(SortableEntry), cmp_ls);
}

const char *sort_ls_label(LsSort s) {
    static char buf[32];
    if (s.key == LS_SORT_HIDDEN) {
        return "Hidden last";
    }
    const char *base;
    switch (s.key) {
        case LS_SORT_NAME: base = "Name"; break;
        case LS_SORT_TYPE: base = "Type"; break;
        case LS_SORT_SIZE: base = "Size"; break;
        case LS_SORT_DATE: base = "Date"; break;
        default: base = "Name"; break;
    }
    snprintf(buf, sizeof(buf), "%s %s", base, s.dir == SORT_ASC ? "A>Z" : "Z>A");
    return buf;
}

typedef struct {
    char label[24];
    int key;
    SortDir dir;
} SortOption;

static size_t build_ls_options(SortOption *out) {
    SortOption opts[] = {
        {"Name A>Z", LS_SORT_NAME, SORT_ASC},
        {"Name Z>A", LS_SORT_NAME, SORT_DESC},
        {"Type", LS_SORT_TYPE, SORT_ASC},
        {"Size large", LS_SORT_SIZE, SORT_DESC},
        {"Size small", LS_SORT_SIZE, SORT_ASC},
        {"Date newest", LS_SORT_DATE, SORT_DESC},
        {"Date oldest", LS_SORT_DATE, SORT_ASC},
        {"Hidden last", LS_SORT_HIDDEN, SORT_ASC},
    };
    memcpy(out, opts, sizeof(opts));
    return sizeof(opts) / sizeof(opts[0]);
}

static size_t build_trash_options(SortOption *out) {
    SortOption opts[] = {
        {"Date newest", TRASH_SORT_DATE, SORT_DESC},
        {"Date oldest", TRASH_SORT_DATE, SORT_ASC},
        {"Name A>Z", TRASH_SORT_NAME, SORT_ASC},
        {"Name Z>A", TRASH_SORT_NAME, SORT_DESC},
        {"Size large", TRASH_SORT_SIZE, SORT_DESC},
        {"Size small", TRASH_SORT_SIZE, SORT_ASC},
        {"Type dir", TRASH_SORT_TYPE, SORT_ASC},
        {"Type file", TRASH_SORT_TYPE, SORT_DESC},
    };
    memcpy(out, opts, sizeof(opts));
    return sizeof(opts) / sizeof(opts[0]);
}

static size_t build_log_options(SortOption *out) {
    SortOption opts[] = {
        {"Date newest", LOG_SORT_DATE, SORT_DESC},
        {"Date oldest", LOG_SORT_DATE, SORT_ASC},
        {"Kind A>Z", LOG_SORT_KIND, SORT_ASC},
        {"Kind Z>A", LOG_SORT_KIND, SORT_DESC},
        {"Status done", LOG_SORT_STATUS, SORT_ASC},
        {"Status err", LOG_SORT_STATUS, SORT_DESC},
    };
    memcpy(out, opts, sizeof(opts));
    return sizeof(opts) / sizeof(opts[0]);
}

#define COLS_PER_ROW 4

static int read_raw_byte(void) {
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) {
        return -1;
    }
    return (unsigned char)c;
}

SortPickResult sort_show_picker(const char *kind, int current_key, SortDir current_dir, int anchor_row) {
    SortOption opts[8];
    size_t opt_count;
    if (strcmp(kind, "trash") == 0) {
        opt_count = build_trash_options(opts);
    } else if (strcmp(kind, "log") == 0) {
        opt_count = build_log_options(opts);
    } else {
        opt_count = build_ls_options(opts);
    }

    size_t sel_idx = 0;
    for (size_t i = 0; i < opt_count; i++) {
        if (opts[i].key == current_key && opts[i].dir == current_dir) {
            sel_idx = i;
            break;
        }
    }

    size_t max_label = 0;
    for (size_t i = 0; i < opt_count; i++) {
        size_t l = strlen(opts[i].label);
        if (l > max_label) {
            max_label = l;
        }
    }
    int col_w = (int)max_label + 5;
    int rows = (int)((opt_count + COLS_PER_ROW - 1) / COLS_PER_ROW);
    int box_inner_w = COLS_PER_ROW * col_w;
    int box_h = rows + 2;
    int top_border_len = box_inner_w + 2;

    int start_row = anchor_row - box_h;
    if (start_row < 1) {
        start_row = 1;
    }

    SortPickResult result;
    memset(&result, 0, sizeof(result));

    for (;;) {
        StrBuf out;
        strbuf_init(&out);
        char buf[64];

        snprintf(buf, sizeof(buf), "\x1b[%d;1H\x1b[2K\x1b[0m", start_row);
        strbuf_push_str(&out, buf);
        strbuf_push_str(&out, "\x1b[2m\xe2\x94\x8c\xe2\x94\x80 Sort by ");
        int header_len = 9;
        int remaining = top_border_len - 2 - header_len;
        for (int i = 0; i < (remaining > 0 ? remaining : 0); i++) {
            strbuf_push_str(&out, "\xe2\x94\x80");
        }
        strbuf_push_str(&out, "\xe2\x94\x90\x1b[0m");

        for (int r = 0; r < rows; r++) {
            snprintf(buf, sizeof(buf), "\x1b[%d;1H\x1b[2K\x1b[0m", start_row + 1 + r);
            strbuf_push_str(&out, buf);
            strbuf_push_str(&out, "\x1b[2m\xe2\x94\x82\x1b[0m");

            for (int c = 0; c < COLS_PER_ROW; c++) {
                size_t i = (size_t)(r * COLS_PER_ROW + c);
                if (i >= opt_count) {
                    for (int k = 0; k < col_w; k++) {
                        strbuf_push_char(&out, ' ');
                    }
                    continue;
                }
                const SortOption *opt = &opts[i];
                int active = (opt->key == current_key && opt->dir == current_dir);
                int is_sel = (i == sel_idx);

                const char *bullet = active ? "\xe2\x97\x8f" : "\xe2\x97\x8b";
                char raw_cell[64];
                snprintf(raw_cell, sizeof(raw_cell), " %s %s", bullet, opt->label);
                size_t raw_len = tui_visible_len(raw_cell);

                if (is_sel) {
                    char padded[80];
                    snprintf(padded, sizeof(padded), "%s", raw_cell);
                    size_t plen = strlen(padded);
                    for (size_t k = raw_len; k < (size_t)col_w && plen + 1 < sizeof(padded); k++) {
                        padded[plen++] = ' ';
                    }
                    padded[plen] = '\0';
                    strbuf_push_str(&out, "\x1b[47;30;1m");
                    strbuf_push_str(&out, padded);
                    strbuf_push_str(&out, "\x1b[0m");
                } else if (active) {
                    strbuf_push_str(&out, "\x1b[36m \xe2\x97\x8f \x1b[0m\x1b[37m");
                    strbuf_push_str(&out, opt->label);
                    strbuf_push_str(&out, "\x1b[0m");
                    size_t used = 3 + strlen(opt->label);
                    for (size_t k = used; k < (size_t)col_w; k++) {
                        strbuf_push_char(&out, ' ');
                    }
                } else {
                    strbuf_push_str(&out, "\x1b[2m \xe2\x97\x8b \x1b[0m\x1b[2m");
                    strbuf_push_str(&out, opt->label);
                    strbuf_push_str(&out, "\x1b[0m");
                    size_t used = 3 + strlen(opt->label);
                    for (size_t k = used; k < (size_t)col_w; k++) {
                        strbuf_push_char(&out, ' ');
                    }
                }
            }
            strbuf_push_str(&out, "\x1b[2m\xe2\x94\x82\x1b[0m");
        }

        snprintf(buf, sizeof(buf), "\x1b[%d;1H\x1b[2K\x1b[0m", start_row + box_h - 1);
        strbuf_push_str(&out, buf);
        strbuf_push_str(&out, "\x1b[2m\xe2\x94\x94");
        for (int i = 0; i < top_border_len - 2; i++) {
            strbuf_push_str(&out, "\xe2\x94\x80");
        }
        strbuf_push_str(&out, "\xe2\x94\x98\x1b[0m");

        fputs(out.data, stdout);
        fflush(stdout);
        strbuf_free(&out);

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
                result.picked = 0;
                goto clear_and_return;
            }
            int c3 = read_raw_byte();
            if (c3 == 'C') {
                if (sel_idx + 1 < opt_count) {
                    sel_idx++;
                }
            } else if (c3 == 'D') {
                if (sel_idx > 0) {
                    sel_idx--;
                }
            } else if (c3 == 'B') {
                if (sel_idx + COLS_PER_ROW < opt_count) {
                    sel_idx += COLS_PER_ROW;
                } else {
                    sel_idx = opt_count - 1;
                }
            } else if (c3 == 'A') {
                if (sel_idx >= (size_t)COLS_PER_ROW) {
                    sel_idx -= COLS_PER_ROW;
                } else {
                    sel_idx = 0;
                }
            }
            continue;
        }

        if (c == '\r' || c == '\n') {
            result.picked = 1;
            result.key = opts[sel_idx].key;
            result.dir = opts[sel_idx].dir;
            goto clear_and_return;
        }
    }

clear_and_return:
    for (int i = 0; i < box_h; i++) {
        printf("\x1b[%d;1H\x1b[2K", start_row + i);
    }
    fflush(stdout);
    return result;
}
