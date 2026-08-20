#include "bookmarks/bookmark_picker.h"

#include "bookmarks/bookmarks.h"
#include "platform/platform.h"
#include "tui/tui.h"

#include <stdio.h>
#include <stdlib.h>
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

static int vis_rows(void) {
    int r = tui_rows() - 2 - 2;
    return r > 1 ? r : 1;
}

static void draw(size_t sel, size_t scroll) {
    tui_clear_screen();

    NavRows nr;
    navrows_init(&nr);
    size_t r0 = navrows_add_row(&nr);
    navrows_add_item(&nr, r0, "Nav", "Navigate", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "Ent", "Go to folder", NAV_DEFAULT);
    navrows_add_item(&nr, r0, "X", "Remove bookmark", NAV_RED);
    navrows_add_item(&nr, r0, "Esc", "Cancel", NAV_DEFAULT);
    tui_draw_navbar(&nr);
    navrows_free(&nr);

    size_t count;
    const Bookmark *bms = bookmarks_get(&count);
    int start = 4;
    int v = vis_rows();

    if (count == 0) {
        printf("\x1b[%d;1H\x1b[2K\x1b[2m  (no bookmarks yet \xe2\x80\x94 press B on a directory in ls/dir to bookmark it)\x1b[0m",
               start);
        for (int i = 1; i < v; i++) {
            printf("\x1b[%d;1H\x1b[2K", start + i);
        }
    } else {
        for (int i = 0; i < v; i++) {
            printf("\x1b[%d;1H\x1b[2K", start + i);
            size_t idx = scroll + (size_t)i;
            if (idx >= count) {
                continue;
            }
            const Bookmark *bm = &bms[idx];
            int is_active = (idx == sel);

            char name_str[560];
            snprintf(name_str, sizeof(name_str), "%s/", bm->name);
            char sub[4096];
            bookmarks_homify(bm->full_path, sub, sizeof(sub));

            int cols = tui_cols();
            int sub_len = (int)tui_visible_len(sub);
            int left_w = cols - sub_len - 3;
            if (left_w < 4) {
                left_w = 4;
            }

            char raw_left[600];
            snprintf(raw_left, sizeof(raw_left), "  %s", name_str);

            if (is_active) {
                char *padded = tui_pad_or_trim(raw_left, left_w);
                printf("\x1b[47;30;1m%s  \x1b[0m\x1b[47;2m%s\x1b[0m", padded, sub);
                free(padded);
            } else {
                char *padded = tui_pad_or_trim(raw_left, left_w);
                printf("\x1b[38;2;255;213;128;1m%s\x1b[0m  \x1b[2m%s\x1b[0m", padded, sub);
                free(padded);
            }
        }
    }

    char left[64];
    if (count == 0) {
        snprintf(left, sizeof(left), "Bookmarks  (empty)");
    } else {
        snprintf(left, sizeof(left), "Bookmarks  %zu folder%s", count, count == 1 ? "" : "s");
    }
    tui_draw_footer((int)count, (int)scroll, v, left);
}

BookmarkPickerResult bookmark_picker_show(void) {
    bookmarks_load();

    size_t sel = 0;
    size_t scroll = 0;

    platform_raw_mode_enable();
    tui_enter_alt();

    BookmarkPickerResult result;
    memset(&result, 0, sizeof(result));

    for (;;) {
        size_t count;
        const Bookmark *bms = bookmarks_get(&count);

        int v = vis_rows();
        if (sel < scroll) {
            scroll = sel;
        }
        if (count > 0 && sel >= scroll + (size_t)v) {
            scroll = sel - (size_t)v + 1;
        }

        draw(sel, scroll);
        fflush(stdout);

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
                result.selected = 0;
                return result;
            }
            int c3 = read_raw_byte();
            if (c3 == 'A') {
                if (sel > 0) {
                    sel--;
                }
            } else if (c3 == 'B') {
                if (count > 0 && sel + 1 < count) {
                    sel++;
                }
            }
            continue;
        }

        if (c == '\r' || c == '\n') {
            if (count > 0) {
                snprintf(result.path, sizeof(result.path), "%s", bms[sel].full_path);
                result.selected = 1;
            }
            tui_exit_alt();
            platform_raw_mode_disable();
            return result;
        }

        if (c == 'x' || c == 'X') {
            if (count > 0) {
                bookmarks_remove_by_id(bms[sel].id);
                size_t new_count;
                bookmarks_get(&new_count);
                if (new_count > 0 && sel >= new_count) {
                    sel = new_count - 1;
                }
            }
            continue;
        }
    }
}
