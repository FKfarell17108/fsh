#include "fileops/general_history_viewer.h"

#include "fileops/activity_log.h"
#include "platform/platform.h"
#include "tui/tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef enum { CAT_COMMANDS, CAT_FILE_MUTATIONS, CAT_TRASH_OPS, CAT_COUNT } Category;

#define PREVIEW_COUNT 5

static Category category_of(const char *kind) {
    if (strcmp(kind, "command") == 0) {
        return CAT_COMMANDS;
    }
    if (strcmp(kind, "trash") == 0 || strcmp(kind, "restore") == 0 || strcmp(kind, "delete") == 0 ||
        strcmp(kind, "empty_trash") == 0) {
        return CAT_TRASH_OPS;
    }
    return CAT_FILE_MUTATIONS;
}

static const char *CAT_LABEL[CAT_COUNT] = {"Commands", "File & Folder Mutations", "Trash Operations"};
static const char *CAT_EMPTY_MSG[CAT_COUNT] = {"no commands yet", "no file operations yet", "no trash activity yet"};
static const char *CAT_COLOR_CODE[CAT_COUNT] = {"32", "36", "33"};

static const char *kind_tag_raw(const char *kind) {
    if (strcmp(kind, "copy") == 0) return "copy   ";
    if (strcmp(kind, "move") == 0) return "move   ";
    if (strcmp(kind, "rename") == 0) return "rename ";
    if (strcmp(kind, "trash") == 0) return "trash  ";
    if (strcmp(kind, "restore") == 0) return "restore";
    if (strcmp(kind, "delete") == 0) return "delete ";
    if (strcmp(kind, "empty_trash") == 0) return "empty  ";
    return "";
}

static void fmt_time(long long ts, char *out, size_t out_size) {
    time_t sec = (time_t)(ts / 1000);
    struct tm tmv;
    localtime_r(&sec, &tmv);
    strftime(out, out_size, "%H:%M", &tmv);
}

typedef enum { ROW_CAT_HEADER, ROW_CAT_EMPTY, ROW_SHOW_MORE, ROW_ENTRY } GhRowKind;

typedef struct {
    GhRowKind kind;
    Category cat;
    size_t event_idx;
    size_t count;
    size_t remaining;
} GhRow;

static size_t collect_category(Category cat, size_t *out_idx, size_t max_out) {
    size_t count;
    const GeneralEvent *events = activity_log_events(&count);
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
        if (category_of(events[i].kind) == cat) {
            if (n < max_out) {
                out_idx[n] = i;
            }
            n++;
        }
    }
    return n;
}

static GhRow *build_rows(const int expanded[CAT_COUNT], size_t *out_count) {
    size_t capacity = 128;
    GhRow *rows = malloc(capacity * sizeof(GhRow));
    size_t count = 0;

    for (int c = 0; c < CAT_COUNT; c++) {
        size_t idx_buf[4096];
        size_t n = collect_category((Category)c, idx_buf, 4096);

        if (count == capacity) {
            capacity *= 2;
            rows = realloc(rows, capacity * sizeof(GhRow));
        }
        GhRow header = {ROW_CAT_HEADER, (Category)c, 0, n, 0};
        rows[count++] = header;

        if (n == 0) {
            if (count == capacity) {
                capacity *= 2;
                rows = realloc(rows, capacity * sizeof(GhRow));
            }
            GhRow empty = {ROW_CAT_EMPTY, (Category)c, 0, 0, 0};
            rows[count++] = empty;
            continue;
        }

        size_t show_n = expanded[c] ? n : (n < PREVIEW_COUNT ? n : PREVIEW_COUNT);
        for (size_t i = 0; i < show_n; i++) {
            if (count == capacity) {
                capacity *= 2;
                rows = realloc(rows, capacity * sizeof(GhRow));
            }
            GhRow entry = {ROW_ENTRY, (Category)c, idx_buf[i], 0, 0};
            rows[count++] = entry;
        }
        if (!expanded[c] && n > PREVIEW_COUNT) {
            if (count == capacity) {
                capacity *= 2;
                rows = realloc(rows, capacity * sizeof(GhRow));
            }
            GhRow more = {ROW_SHOW_MORE, (Category)c, 0, 0, n - PREVIEW_COUNT};
            rows[count++] = more;
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
    int r = tui_rows() - 2 - 2;
    return r > 1 ? r : 1;
}

static void draw_main(GhRow *rows, size_t row_count, size_t sel, size_t scroll) {
    tui_clear_screen();

    size_t total;
    const GeneralEvent *events = activity_log_events(&total);

    NavRows nr;
    navrows_init(&nr);
    size_t r0 = navrows_add_row(&nr);
    navrows_add_item(&nr, r0, "Nav", "Navigate", NAV_DEFAULT);
    const char *ent_label = "Expand";
    if (row_count > 0 && rows[sel].kind == ROW_ENTRY) {
        ent_label = "Detail";
    }
    navrows_add_item(&nr, r0, "Ent", ent_label, NAV_DEFAULT);
    navrows_add_item(&nr, r0, "Esc", "Back", NAV_DEFAULT);
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
        const GhRow *row = &rows[idx];
        int is_active = (idx == sel);

        if (row->kind == ROW_CAT_HEADER) {
            char raw[256];
            char count_str[32];
            if (row->count > 0) {
                snprintf(count_str, sizeof(count_str), "  (%zu)", row->count);
            } else {
                snprintf(count_str, sizeof(count_str), "  (empty)");
            }
            snprintf(raw, sizeof(raw), "%s %s%s", "\xe2\x96\xb8", CAT_LABEL[row->cat], count_str);
            char *padded = tui_pad_or_trim(raw, cols);
            if (is_active) {
                printf("\x1b[47;30;1m%s\x1b[0m", padded);
            } else {
                printf("\x1b[%s;1m%s\x1b[0m", CAT_COLOR_CODE[row->cat], padded);
            }
            free(padded);
        } else if (row->kind == ROW_CAT_EMPTY) {
            char raw[128];
            snprintf(raw, sizeof(raw), "    %s", CAT_EMPTY_MSG[row->cat]);
            char *padded = tui_pad_or_trim(raw, cols);
            if (is_active) {
                printf("\x1b[47;30;1m%s\x1b[0m", padded);
            } else {
                printf("\x1b[2m%s\x1b[0m", padded);
            }
            free(padded);
        } else if (row->kind == ROW_SHOW_MORE) {
            char raw[128];
            snprintf(raw, sizeof(raw), "    \xe2\x86\x93 %zu more \xe2\x80\x94 press enter to show", row->remaining);
            char *padded = tui_pad_or_trim(raw, cols);
            if (is_active) {
                printf("\x1b[47;30;1m%s\x1b[0m", padded);
            } else {
                printf("\x1b[%s;1m%s\x1b[0m", CAT_COLOR_CODE[row->cat], padded);
            }
            free(padded);
        } else {
            const GeneralEvent *e = &events[row->event_idx];
            const char *tag = kind_tag_raw(e->kind);
            char time_str[16];
            fmt_time(e->ts, time_str, sizeof(time_str));
            int time_len = (int)strlen(time_str);
            int left_w = cols - time_len - 2;
            if (left_w < 4) {
                left_w = 4;
            }

            char raw_left[4200];
            snprintf(raw_left, sizeof(raw_left), "    %s%s", tag, e->label);
            char *padded = tui_pad_or_trim(raw_left, left_w);

            if (is_active) {
                printf("\x1b[47;30;1m%s  \x1b[0m\x1b[47;30;1m%s\x1b[0m", padded, time_str);
            } else {
                printf("\x1b[%sm%s\x1b[0m  \x1b[2m%s\x1b[0m", CAT_COLOR_CODE[row->cat], padded, time_str);
            }
            free(padded);
        }
    }

    char left[128];
    if (total > 0) {
        snprintf(left, sizeof(left), "Activity  %zu event%s", total, total == 1 ? "" : "s");
    } else {
        snprintf(left, sizeof(left), "Activity  (no events yet)");
    }
    tui_draw_footer((int)row_count, (int)scroll, v, left);
    fflush(stdout);
}

static void draw_detail(const GeneralEvent *e) {
    tui_clear_screen();
    NavRows nr;
    navrows_init(&nr);
    size_t r0 = navrows_add_row(&nr);
    navrows_add_item(&nr, r0, "Esc", "Back", NAV_DEFAULT);
    tui_draw_navbar(&nr);
    navrows_free(&nr);

    int start = 4;
    int line_no = 0;
    Category cat = category_of(e->kind);
    char time_str[16];
    fmt_time(e->ts, time_str, sizeof(time_str));

    printf("\x1b[%d;1H\x1b[2K", start + line_no++);
    printf("\x1b[%d;1H\x1b[2K  \x1b[%sm%s\x1b[0m\x1b[2m  \xc2\xb7  \x1b[0m%s  \x1b[2m%s\x1b[0m", start + line_no++,
           CAT_COLOR_CODE[cat], CAT_LABEL[cat], kind_tag_raw(e->kind), time_str);
    printf("\x1b[%d;1H\x1b[2K  \x1b[2mid: %s\x1b[0m", start + line_no++, e->id);
    printf("\x1b[%d;1H\x1b[2K", start + line_no++);
    printf("\x1b[%d;1H\x1b[2K  \x1b[2mwhat\x1b[0m", start + line_no++);
    printf("\x1b[%d;1H\x1b[2K  %s", start + line_no++, e->label);

    if (e->detail[0] != '\0') {
        printf("\x1b[%d;1H\x1b[2K", start + line_no++);
        printf("\x1b[%d;1H\x1b[2K  \x1b[2mdetail\x1b[0m", start + line_no++);
        char detail_copy[512];
        snprintf(detail_copy, sizeof(detail_copy), "%s", e->detail);
        char *saveptr = NULL;
        char *dl = strtok_r(detail_copy, "\n", &saveptr);
        while (dl) {
            printf("\x1b[%d;1H\x1b[2K  %s", start + line_no++, dl);
            dl = strtok_r(NULL, "\n", &saveptr);
        }
    }

    char footer_left[64];
    snprintf(footer_left, sizeof(footer_left), "%.40s", e->label);
    tui_draw_bottom_bar(footer_left, "");
    fflush(stdout);
}

static void show_detail_screen(const GeneralEvent *e) {
    for (;;) {
        draw_detail(e);
        int c = read_raw_byte();
        if (c < 0) {
            continue;
        }
        if (c == 3) {
            return;
        }
        if (c == 0x1b) {
            int c2 = tui_read_byte_after_esc();
            if (c2 == '[') {
                read_raw_byte();
                continue;
            }
            return;
        }
        if (c == 'q') {
            return;
        }
    }
}

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} IdSet;

static int idset_has(const IdSet *s, const char *id) {
    for (size_t i = 0; i < s->count; i++) {
        if (strcmp(s->items[i], id) == 0) {
            return 1;
        }
    }
    return 0;
}

static void idset_toggle(IdSet *s, const char *id) {
    for (size_t i = 0; i < s->count; i++) {
        if (strcmp(s->items[i], id) == 0) {
            free(s->items[i]);
            for (size_t j = i; j + 1 < s->count; j++) {
                s->items[j] = s->items[j + 1];
            }
            s->count--;
            return;
        }
    }
    if (s->count == s->capacity) {
        s->capacity = s->capacity ? s->capacity * 2 : 16;
        s->items = realloc(s->items, s->capacity * sizeof(char *));
    }
    s->items[s->count++] = strdup(id);
}

static void idset_clear(IdSet *s) {
    for (size_t i = 0; i < s->count; i++) {
        free(s->items[i]);
    }
    s->count = 0;
}

static void draw_edit(Category cat, size_t *idx_buf, size_t idx_count, size_t sel, size_t scroll,
                       const IdSet *selected) {
    tui_clear_screen();
    size_t total;
    const GeneralEvent *events = activity_log_events(&total);
    int is_cmd = (cat == CAT_COMMANDS);

    NavRows nr;
    navrows_init(&nr);
    size_t r0 = navrows_add_row(&nr);
    navrows_add_item(&nr, r0, "Nav", "Navigate", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "Spc", "Select", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "A", "Select All", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "Ent", is_cmd ? "Use" : "Detail", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "X", "Delete", NAV_RED);
    navrows_add_item(&nr, r0, "D", "Delete All", NAV_RED);
    navrows_add_item(&nr, r0, "Esc", selected->count > 0 ? "Deselect" : "Back", NAV_DEFAULT);
    tui_draw_navbar(&nr);
    navrows_free(&nr);

    int start = 4;
    int v = vis_rows();
    int cols = tui_cols();

    if (idx_count == 0) {
        printf("\x1b[%d;1H\x1b[2K\x1b[2m  (%s)\x1b[0m", start, CAT_EMPTY_MSG[cat]);
        for (int i = 1; i < v; i++) {
            printf("\x1b[%d;1H\x1b[2K", start + i);
        }
    } else {
        for (int i = 0; i < v; i++) {
            printf("\x1b[%d;1H\x1b[2K", start + i);
            size_t idx = scroll + (size_t)i;
            if (idx >= idx_count) {
                continue;
            }
            const GeneralEvent *e = &events[idx_buf[idx]];
            int is_active = (idx == sel);
            int is_sel = idset_has(selected, e->id);

            char time_str[16];
            fmt_time(e->ts, time_str, sizeof(time_str));
            int time_len = (int)strlen(time_str);
            int left_w = cols - time_len - 2;
            if (left_w < 4) {
                left_w = 4;
            }

            char raw_left[4200];
            if (is_cmd) {
                snprintf(raw_left, sizeof(raw_left), "    %s", e->label);
            } else {
                snprintf(raw_left, sizeof(raw_left), "    %s %s", kind_tag_raw(e->kind), e->label);
            }
            char *padded = tui_pad_or_trim(raw_left, left_w);

            if (is_active && is_sel) {
                printf("\x1b[45;37;1m%s  \x1b[0m\x1b[45;37;1m%s\x1b[0m", padded, time_str);
            } else if (is_active) {
                printf("\x1b[47;30;1m%s  \x1b[0m\x1b[47;30;1m%s\x1b[0m", padded, time_str);
            } else if (is_sel) {
                printf("\x1b[35;1m%s  \x1b[0m\x1b[35;1m%s\x1b[0m", padded, time_str);
            } else {
                printf("\x1b[%sm%s\x1b[0m  \x1b[2m%s\x1b[0m", CAT_COLOR_CODE[cat], padded, time_str);
            }
            free(padded);
        }
    }

    char left[128];
    if (selected->count > 0) {
        snprintf(left, sizeof(left), "%s  %zu  \x1b[35m%zu sel\x1b[0m", CAT_LABEL[cat], idx_count, selected->count);
    } else {
        snprintf(left, sizeof(left), "%s  %zu", CAT_LABEL[cat], idx_count);
    }
    tui_draw_footer((int)idx_count, (int)scroll, v, left);
    fflush(stdout);
}

static void open_category_edit(Category cat) {
    IdSet selected = {0};
    size_t sel = 0;
    size_t scroll = 0;

    for (;;) {
        size_t idx_buf[4096];
        size_t idx_count = collect_category(cat, idx_buf, 4096);
        if (idx_count == 0) {
            sel = 0;
        } else if (sel >= idx_count) {
            sel = idx_count - 1;
        }

        int v = vis_rows();
        if (sel < scroll) {
            scroll = sel;
        }
        if (idx_count > 0 && sel >= scroll + (size_t)v) {
            scroll = sel - (size_t)v + 1;
        }

        draw_edit(cat, idx_buf, idx_count, sel, scroll, &selected);

        int c = read_raw_byte();
        if (c < 0) {
            continue;
        }

        if (c == 0x1b) {
            int c2 = tui_read_byte_after_esc();
            if (c2 != '[') {
                if (selected.count > 0) {
                    idset_clear(&selected);
                    continue;
                }
                idset_clear(&selected);
                free(selected.items);
                return;
            }
            int c3 = read_raw_byte();
            if (c3 == 'A') {
                if (sel > 0) {
                    sel--;
                }
            } else if (c3 == 'B') {
                if (idx_count > 0 && sel + 1 < idx_count) {
                    sel++;
                }
            }
            continue;
        }

        if (c == 3) {
            idset_clear(&selected);
            free(selected.items);
            return;
        }

        if (c == ' ') {
            if (idx_count == 0) {
                continue;
            }
            size_t total;
            const GeneralEvent *events = activity_log_events(&total);
            idset_toggle(&selected, events[idx_buf[sel]].id);
            continue;
        }

        if (c == 'a') {
            if (selected.count == idx_count) {
                idset_clear(&selected);
            } else {
                idset_clear(&selected);
                size_t total;
                const GeneralEvent *events = activity_log_events(&total);
                for (size_t i = 0; i < idx_count; i++) {
                    idset_toggle(&selected, events[idx_buf[i]].id);
                }
            }
            continue;
        }

        if (c == 'x' || c == 127) {
            if (idx_count == 0) {
                continue;
            }
            size_t total;
            const GeneralEvent *events = activity_log_events(&total);
            char title[64];
            if (selected.count > 0) {
                snprintf(title, sizeof(title), "Delete %zu items?", selected.count);
            } else {
                snprintf(title, sizeof(title), "Delete event?");
            }
            TuiPopupResult r = tui_popup_input(title, NULL, 0, NULL, 0, NULL, NULL);
            if (r.confirmed) {
                if (selected.count > 0) {
                    for (size_t i = 0; i < selected.count; i++) {
                        if (cat == CAT_COMMANDS) {
                            size_t total2;
                            const GeneralEvent *ev2 = activity_log_events(&total2);
                            for (size_t j = 0; j < total2; j++) {
                                if (strcmp(ev2[j].id, selected.items[i]) == 0) {
                                    activity_log_delete_command_events(ev2[j].label);
                                    break;
                                }
                            }
                        }
                    }
                    idset_clear(&selected);
                } else {
                    const GeneralEvent *e = &events[idx_buf[sel]];
                    if (cat == CAT_COMMANDS) {
                        activity_log_delete_command_events(e->label);
                    }
                }
            }
            continue;
        }

        if (c == 'd') {
            char title[64];
            snprintf(title, sizeof(title), "Delete all %s?", CAT_LABEL[cat]);
            TuiPopupResult r = tui_popup_input(title, NULL, 0, NULL, 0, NULL, NULL);
            if (r.confirmed) {
                if (cat == CAT_COMMANDS) {
                    activity_log_delete_all_command_events();
                }
                idset_clear(&selected);
                free(selected.items);
                return;
            }
            continue;
        }

        if (c == '\r' || c == '\n') {
            if (idx_count == 0) {
                continue;
            }
            size_t total;
            const GeneralEvent *events = activity_log_events(&total);
            if (cat == CAT_COMMANDS) {
                continue;
            }
            show_detail_screen(&events[idx_buf[sel]]);
            continue;
        }
    }
}

void general_history_viewer_show(void) {
    activity_log_load();
    activity_log_prune_stale_commands();

    int expanded[CAT_COUNT] = {0};
    size_t row_count;
    GhRow *rows = build_rows(expanded, &row_count);
    size_t sel = 0;
    size_t scroll = 0;

    platform_raw_mode_enable();
    tui_enter_alt();

    for (;;) {
        int v = vis_rows();
        if (sel < scroll) {
            scroll = sel;
        }
        if (row_count > 0 && sel >= scroll + (size_t)v) {
            scroll = sel - (size_t)v + 1;
        }

        draw_main(rows, row_count, sel, scroll);

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
                if (sel > 0) {
                    sel--;
                }
            } else if (c3 == 'B') {
                if (row_count > 0 && sel + 1 < row_count) {
                    sel++;
                }
            }
            continue;
        }

        if (c == 3 || c == 'q') {
            break;
        }

        if (c == '\r' || c == '\n') {
            if (row_count == 0) {
                continue;
            }
            GhRow row = rows[sel];
            if (row.kind == ROW_CAT_HEADER) {
                open_category_edit(row.cat);
                free(rows);
                rows = build_rows(expanded, &row_count);
                if (sel >= row_count && row_count > 0) {
                    sel = row_count - 1;
                }
                continue;
            }
            if (row.kind == ROW_CAT_EMPTY) {
                continue;
            }
            if (row.kind == ROW_SHOW_MORE) {
                expanded[row.cat] = 1;
                free(rows);
                rows = build_rows(expanded, &row_count);
                continue;
            }
            if (row.kind == ROW_ENTRY) {
                size_t total;
                const GeneralEvent *events = activity_log_events(&total);
                show_detail_screen(&events[row.event_idx]);
            }
            continue;
        }
    }

    tui_exit_alt();
    platform_raw_mode_disable();
    free(rows);
}
