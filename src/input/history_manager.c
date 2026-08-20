#include "input/history_manager.h"

#include "fileops/activity_log.h"
#include "input/history.h"
#include "platform/platform.h"
#include "tui/tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char cmd[4096];
    long long ts;
} HmEntry;

typedef struct {
    char label[16];
    HmEntry *entries;
    size_t count;
    size_t capacity;
} HmBucket;

#define BUCKET_COUNT 5

typedef enum { HM_ROW_HEADER, HM_ROW_ENTRY } HmRowKind;

typedef struct {
    HmRowKind kind;
    size_t bucket_idx;
    size_t entry_idx;
} HmRow;

static void bucket_push(HmBucket *b, HmEntry e) {
    if (b->count == b->capacity) {
        b->capacity = b->capacity ? b->capacity * 2 : 16;
        b->entries = realloc(b->entries, b->capacity * sizeof(HmEntry));
    }
    b->entries[b->count++] = e;
}

static void bucket_remove_cmd(HmBucket *b, const char *cmd) {
    size_t w = 0;
    for (size_t i = 0; i < b->count; i++) {
        if (strcmp(b->entries[i].cmd, cmd) != 0) {
            b->entries[w++] = b->entries[i];
        }
    }
    b->count = w;
}

static void group_by_time(HmBucket buckets[BUCKET_COUNT]) {
    static const char *labels[BUCKET_COUNT] = {"Last hour", "Today", "Yesterday", "This week", "Older"};
    for (int i = 0; i < BUCKET_COUNT; i++) {
        snprintf(buckets[i].label, sizeof(buckets[i].label), "%s", labels[i]);
        buckets[i].entries = NULL;
        buckets[i].count = 0;
        buckets[i].capacity = 0;
    }

    time_t now_sec = time(NULL);
    struct tm tmv;
    localtime_r(&now_sec, &tmv);
    tmv.tm_hour = 0;
    tmv.tm_min = 0;
    tmv.tm_sec = 0;
    long long today_ms = (long long)mktime(&tmv) * 1000;
    long long yesterday_ms = today_ms - 86400000LL;
    long long week_ms = today_ms - 7 * 86400000LL;
    long long now_ms = (long long)now_sec * 1000;

    size_t count = history_count();
    for (size_t i = 0; i < count; i++) {
        char cmd[4096];
        long long ts;
        if (!history_entry_at(i, cmd, sizeof(cmd), &ts)) {
            continue;
        }
        HmEntry e;
        snprintf(e.cmd, sizeof(e.cmd), "%s", cmd);
        e.ts = ts;

        long long age = now_ms - ts;
        if (ts == 0 || ts < week_ms) {
            bucket_push(&buckets[4], e);
        } else if (ts < yesterday_ms) {
            bucket_push(&buckets[3], e);
        } else if (ts < today_ms) {
            bucket_push(&buckets[2], e);
        } else if (age < 3600000LL) {
            bucket_push(&buckets[0], e);
        } else {
            bucket_push(&buckets[1], e);
        }
    }
}

static HmRow *build_rows(HmBucket buckets[BUCKET_COUNT], size_t *out_count) {
    size_t capacity = 64;
    HmRow *rows = malloc(capacity * sizeof(HmRow));
    size_t count = 0;
    for (size_t bi = 0; bi < BUCKET_COUNT; bi++) {
        if (buckets[bi].count == 0) {
            continue;
        }
        if (count == capacity) {
            capacity *= 2;
            rows = realloc(rows, capacity * sizeof(HmRow));
        }
        rows[count].kind = HM_ROW_HEADER;
        rows[count].bucket_idx = bi;
        rows[count].entry_idx = 0;
        count++;
        for (size_t ei = 0; ei < buckets[bi].count; ei++) {
            if (count == capacity) {
                capacity *= 2;
                rows = realloc(rows, capacity * sizeof(HmRow));
            }
            rows[count].kind = HM_ROW_ENTRY;
            rows[count].bucket_idx = bi;
            rows[count].entry_idx = ei;
            count++;
        }
    }
    *out_count = count;
    return rows;
}

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} StrSet;

static int strset_has(const StrSet *s, const char *v) {
    for (size_t i = 0; i < s->count; i++) {
        if (strcmp(s->items[i], v) == 0) {
            return 1;
        }
    }
    return 0;
}

static void strset_add(StrSet *s, const char *v) {
    if (strset_has(s, v)) {
        return;
    }
    if (s->count == s->capacity) {
        s->capacity = s->capacity ? s->capacity * 2 : 16;
        s->items = realloc(s->items, s->capacity * sizeof(char *));
    }
    s->items[s->count++] = strdup(v);
}

static void strset_remove(StrSet *s, const char *v) {
    size_t w = 0;
    for (size_t i = 0; i < s->count; i++) {
        if (strcmp(s->items[i], v) == 0) {
            free(s->items[i]);
            continue;
        }
        s->items[w++] = s->items[i];
    }
    s->count = w;
}

static void strset_clear(StrSet *s) {
    for (size_t i = 0; i < s->count; i++) {
        free(s->items[i]);
    }
    s->count = 0;
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
    int r = tui_rows() - 2 - 2;
    return r > 1 ? r : 1;
}

static size_t total_cmds(HmBucket buckets[BUCKET_COUNT]) {
    size_t n = 0;
    for (int i = 0; i < BUCKET_COUNT; i++) {
        n += buckets[i].count;
    }
    return n;
}

static void draw(HmBucket buckets[BUCKET_COUNT], HmRow *rows, size_t row_count, size_t cursor, size_t scroll,
                  const StrSet *selected) {
    tui_clear_screen();

    NavRows nr;
    navrows_init(&nr);
    size_t r0 = navrows_add_row(&nr);
    navrows_add_item(&nr, r0, "Nav", "Navigate", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "Spc", "Select", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "A", "Select All", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "Ent", "Use Command", NAV_GREEN);
    navrows_add_item(&nr, r0, "X", "Delete", NAV_RED);
    navrows_add_item(&nr, r0, "D", "Delete All", NAV_RED);
    navrows_add_item(&nr, r0, "Esc", selected->count > 0 ? "Deselect" : "Close", NAV_DEFAULT);
    tui_draw_navbar(&nr);
    navrows_free(&nr);

    int start = 4;
    int v = vis_rows();
    int cols = tui_cols();

    for (int i = 0; i < v; i++) {
        printf("\x1b[%d;1H\x1b[2K", start + i);
        size_t idx = scroll + (size_t)i;
        if (idx >= row_count) {
            continue;
        }
        const HmRow *row = &rows[idx];
        int is_active = (idx == cursor);

        if (row->kind == HM_ROW_HEADER) {
            const HmBucket *b = &buckets[row->bucket_idx];
            int all_sel = b->count > 0;
            for (size_t k = 0; k < b->count && all_sel; k++) {
                if (!strset_has(selected, b->entries[k].cmd)) {
                    all_sel = 0;
                }
            }
            char raw[512];
            snprintf(raw, sizeof(raw), "    %s  (%zu commands)", b->label, b->count);
            char *padded = tui_pad_or_trim(raw, cols);
            if (is_active && all_sel) {
                printf("\x1b[45;37;1m%s\x1b[0m", padded);
            } else if (is_active) {
                printf("\x1b[43;30;1m%s\x1b[0m", padded);
            } else if (all_sel) {
                printf("\x1b[35;1m%s\x1b[0m", padded);
            } else {
                printf("\x1b[33;1m%s\x1b[0m", padded);
            }
            free(padded);
        } else {
            const HmEntry *e = &buckets[row->bucket_idx].entries[row->entry_idx];
            int is_sel = strset_has(selected, e->cmd);

            char time_str[16] = "     ";
            if (e->ts != 0) {
                time_t sec = (time_t)(e->ts / 1000);
                struct tm tmv;
                localtime_r(&sec, &tmv);
                strftime(time_str, sizeof(time_str), "%H:%M", &tmv);
            }
            int time_len = (int)strlen(time_str);
            int left_w = cols - time_len - 2;
            if (left_w < 4) {
                left_w = 4;
            }

            char raw_left[4200];
            snprintf(raw_left, sizeof(raw_left), "    %s", e->cmd);
            char *padded = tui_pad_or_trim(raw_left, left_w);

            if (is_active && is_sel) {
                printf("\x1b[45;37;1m%s  \x1b[0m\x1b[45;37;1m%s\x1b[0m", padded, time_str);
            } else if (is_active) {
                printf("\x1b[47;30;1m%s  \x1b[0m\x1b[47;30;1m%s\x1b[0m", padded, time_str);
            } else if (is_sel) {
                printf("\x1b[35;1m%s  \x1b[0m\x1b[35;1m%s\x1b[0m", padded, time_str);
            } else {
                printf("%s  \x1b[2m%s\x1b[0m", padded, time_str);
            }
            free(padded);
        }
    }

    char left[128];
    size_t n = total_cmds(buckets);
    if (selected->count > 0) {
        snprintf(left, sizeof(left), "History  %zu command%s  \x1b[35m%zu sel\x1b[0m", n, n == 1 ? "" : "s",
                 selected->count);
    } else {
        snprintf(left, sizeof(left), "History  %zu command%s", n, n == 1 ? "" : "s");
    }
    tui_draw_footer((int)row_count, (int)scroll, v, left);
    fflush(stdout);
}

HistoryManagerResult history_manager_show(void) {
    HistoryManagerResult result;
    memset(&result, 0, sizeof(result));

    if (history_count() == 0) {
        printf("\r\n\x1b[2m  (no command history yet)\x1b[0m\r\n");
        fflush(stdout);
        return result;
    }

    HmBucket buckets[BUCKET_COUNT];
    group_by_time(buckets);

    size_t row_count;
    HmRow *rows = build_rows(buckets, &row_count);

    size_t cursor = 0;
    size_t scroll = 0;
    StrSet selected = {0};

    platform_raw_mode_enable();
    tui_enter_alt();

    for (;;) {
        int v = vis_rows();
        if (cursor < scroll) {
            scroll = cursor;
        }
        if (row_count > 0 && cursor >= scroll + (size_t)v) {
            scroll = cursor - (size_t)v + 1;
        }

        draw(buckets, rows, row_count, cursor, scroll, &selected);

        int c = read_raw_byte();
        if (c < 0) {
            continue;
        }

        if (c == 0x1b) {
            int c2 = tui_read_byte_after_esc();
            if (c2 != '[') {
                if (selected.count > 0) {
                    strset_clear(&selected);
                    continue;
                }
                break;
            }
            int c3 = read_raw_byte();
            if (c3 == 'A') {
                if (cursor > 0) {
                    cursor--;
                }
            } else if (c3 == 'B') {
                if (cursor + 1 < row_count) {
                    cursor++;
                }
            }
            continue;
        }

        if (c == 3 || c == 'q') {
            if (selected.count > 0) {
                strset_clear(&selected);
                continue;
            }
            break;
        }

        if (c == ' ') {
            if (row_count == 0) {
                continue;
            }
            const HmRow *row = &rows[cursor];
            if (row->kind == HM_ROW_HEADER) {
                HmBucket *b = &buckets[row->bucket_idx];
                int all_sel = b->count > 0;
                for (size_t k = 0; k < b->count && all_sel; k++) {
                    if (!strset_has(&selected, b->entries[k].cmd)) {
                        all_sel = 0;
                    }
                }
                for (size_t k = 0; k < b->count; k++) {
                    if (all_sel) {
                        strset_remove(&selected, b->entries[k].cmd);
                    } else {
                        strset_add(&selected, b->entries[k].cmd);
                    }
                }
            } else {
                const char *cmd = buckets[row->bucket_idx].entries[row->entry_idx].cmd;
                if (strset_has(&selected, cmd)) {
                    strset_remove(&selected, cmd);
                } else {
                    strset_add(&selected, cmd);
                }
            }
            continue;
        }

        if (c == 'a') {
            size_t all_count = total_cmds(buckets);
            if (selected.count == all_count) {
                strset_clear(&selected);
            } else {
                strset_clear(&selected);
                for (int bi = 0; bi < BUCKET_COUNT; bi++) {
                    for (size_t k = 0; k < buckets[bi].count; k++) {
                        strset_add(&selected, buckets[bi].entries[k].cmd);
                    }
                }
            }
            continue;
        }

        if (c == 'd') {
            const char *body[] = {"All history entries will be removed."};
            TuiPopupResult r = tui_popup_input("Delete all history?", NULL, 0, body, 1, NULL, NULL);
            if (r.confirmed) {
                for (int bi = 0; bi < BUCKET_COUNT; bi++) {
                    buckets[bi].count = 0;
                }
                history_delete_all();
                activity_log_delete_all_command_events();
                free(rows);
                row_count = 0;
                rows = NULL;
                break;
            }
            continue;
        }

        if (c == 'x' || c == 127) {
            if (row_count == 0) {
                continue;
            }
            const HmRow *row = &rows[cursor];
            if (row->kind == HM_ROW_HEADER && buckets[row->bucket_idx].count == 0) {
                continue;
            }

            char title[64];
            if (selected.count > 0) {
                snprintf(title, sizeof(title), "Delete %zu items?", selected.count);
            } else if (row->kind == HM_ROW_HEADER) {
                snprintf(title, sizeof(title), "Delete bucket?");
            } else {
                snprintf(title, sizeof(title), "Delete command?");
            }

            TuiPopupResult r = tui_popup_input(title, NULL, 0, NULL, 0, NULL, NULL);
            if (r.confirmed) {
                if (selected.count > 0) {
                    for (size_t i = 0; i < selected.count; i++) {
                        for (int bi = 0; bi < BUCKET_COUNT; bi++) {
                            bucket_remove_cmd(&buckets[bi], selected.items[i]);
                        }
                        history_delete_cmd(selected.items[i]);
                        activity_log_delete_command_events(selected.items[i]);
                    }
                    strset_clear(&selected);
                } else if (row->kind == HM_ROW_HEADER) {
                    HmBucket *b = &buckets[row->bucket_idx];
                    for (size_t k = 0; k < b->count; k++) {
                        history_delete_cmd(b->entries[k].cmd);
                        activity_log_delete_command_events(b->entries[k].cmd);
                    }
                    b->count = 0;
                } else {
                    const char *cmd = buckets[row->bucket_idx].entries[row->entry_idx].cmd;
                    history_delete_cmd(cmd);
                    activity_log_delete_command_events(cmd);
                    bucket_remove_cmd(&buckets[row->bucket_idx], cmd);
                }

                free(rows);
                rows = build_rows(buckets, &row_count);
                if (row_count == 0) {
                    break;
                }
                if (cursor >= row_count) {
                    cursor = row_count - 1;
                }
            }
            continue;
        }

        if (c == '\r' || c == '\n') {
            if (row_count == 0) {
                continue;
            }
            const HmRow *row = &rows[cursor];
            if (row->kind == HM_ROW_ENTRY) {
                snprintf(result.cmd, sizeof(result.cmd), "%s",
                         buckets[row->bucket_idx].entries[row->entry_idx].cmd);
                result.selected = 1;
                break;
            }
            continue;
        }
    }

    tui_exit_alt();
    platform_raw_mode_disable();

    free(rows);
    strset_clear(&selected);
    free(selected.items);
    for (int i = 0; i < BUCKET_COUNT; i++) {
        free(buckets[i].entries);
    }

    return result;
}
