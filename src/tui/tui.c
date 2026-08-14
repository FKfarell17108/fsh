#include "tui/tui.h"

#include "platform/platform.h"
#include "util/strbuf.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_NAV_ROWS 2

void navrows_init(NavRows *nr) {
    nr->rows = NULL;
    nr->count = 0;
    nr->capacity = 0;
}

size_t navrows_add_row(NavRows *nr) {
    if (nr->count == nr->capacity) {
        nr->capacity = nr->capacity ? nr->capacity * 2 : 4;
        nr->rows = realloc(nr->rows, nr->capacity * sizeof(NavRow));
    }
    nr->rows[nr->count].items = NULL;
    nr->rows[nr->count].count = 0;
    nr->rows[nr->count].capacity = 0;
    return nr->count++;
}

void navrows_add_item(NavRows *nr, size_t row_index, const char *key, const char *label, NavItemColor color) {
    if (row_index >= nr->count) {
        return;
    }
    NavRow *row = &nr->rows[row_index];
    if (row->count == row->capacity) {
        row->capacity = row->capacity ? row->capacity * 2 : 4;
        row->items = realloc(row->items, row->capacity * sizeof(NavItem));
    }
    NavItem *item = &row->items[row->count++];
    snprintf(item->key, sizeof(item->key), "%s", key);
    snprintf(item->label, sizeof(item->label), "%s", label);
    item->color = color;
}

void navrows_free(NavRows *nr) {
    for (size_t i = 0; i < nr->count; i++) {
        free(nr->rows[i].items);
    }
    free(nr->rows);
    nr->rows = NULL;
    nr->count = 0;
    nr->capacity = 0;
}

int tui_cols(void) {
    int cols;
    int rows;
    platform_get_winsize(&cols, &rows);
    return cols;
}

int tui_rows(void) {
    int cols;
    int rows;
    platform_get_winsize(&cols, &rows);
    return rows;
}

static size_t utf8_char_len(unsigned char c) {
    if ((c & 0x80) == 0) {
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        return 3;
    }
    if ((c & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

size_t tui_visible_len(const char *str) {
    size_t count = 0;
    size_t i = 0;
    size_t len = strlen(str);

    while (i < len) {
        if (str[i] == '\x1b' && i + 1 < len && str[i + 1] == '[') {
            size_t j = i + 2;
            while (j < len && !(str[j] >= '@' && str[j] <= '~')) {
                j++;
            }
            if (j < len) {
                j++;
            }
            i = j;
            continue;
        }
        size_t clen = utf8_char_len((unsigned char)str[i]);
        i += clen;
        count++;
    }

    return count;
}

char *tui_pad_or_trim(const char *str, int width) {
    if (width < 0) {
        width = 0;
    }
    size_t vlen = tui_visible_len(str);

    StrBuf sb;
    strbuf_init(&sb);

    if (vlen < (size_t)width) {
        strbuf_push_str(&sb, str);
        for (size_t i = vlen; i < (size_t)width; i++) {
            strbuf_push_char(&sb, ' ');
        }
        return strbuf_take(&sb);
    }
    if (vlen == (size_t)width) {
        strbuf_push_str(&sb, str);
        return strbuf_take(&sb);
    }

    size_t len = strlen(str);
    size_t i = 0;
    int count = 0;

    while (i < len) {
        if (str[i] == '\x1b' && i + 1 < len && str[i + 1] == '[') {
            size_t j = i + 2;
            while (j < len && !(str[j] >= '@' && str[j] <= '~')) {
                j++;
            }
            if (j < len) {
                j++;
            }
            for (size_t k = i; k < j; k++) {
                strbuf_push_char(&sb, str[k]);
            }
            i = j;
            continue;
        }
        if (count >= width - 1) {
            strbuf_push_str(&sb, "\x1b[0m");
            break;
        }
        size_t clen = utf8_char_len((unsigned char)str[i]);
        for (size_t k = 0; k < clen && i + k < len; k++) {
            strbuf_push_char(&sb, str[i + k]);
        }
        i += clen;
        count++;
    }
    strbuf_push_str(&sb, "\x1b[0m");

    return strbuf_take(&sb);
}

static void ansi_wrap(StrBuf *sb, const char *codes, const char *text) {
    strbuf_push_str(sb, "\x1b[");
    strbuf_push_str(sb, codes);
    strbuf_push_char(sb, 'm');
    strbuf_push_str(sb, text);
    strbuf_push_str(sb, "\x1b[0m");
}

static const char *key_block_codes(NavItemColor color) {
    switch (color) {
        case NAV_GREEN: return "42;30;1";
        case NAV_YELLOW: return "43;30;1";
        case NAV_RED: return "41;37;1";
        case NAV_CYAN: return "46;30;1";
        default: return "47;30;1";
    }
}

static char *render_nav_row(const NavRow *row, int cols) {
    StrBuf sb;
    strbuf_init(&sb);

    if (row->count == 0) {
        strbuf_push_str(&sb, "\x1b[40m");
        for (int i = 0; i < cols; i++) {
            strbuf_push_char(&sb, ' ');
        }
        strbuf_push_str(&sb, "\x1b[0m");
        return strbuf_take(&sb);
    }

    size_t n = row->count;
    size_t key_w = 0;
    for (size_t i = 0; i < n; i++) {
        size_t klen = strlen(row->items[i].key);
        if (klen > key_w) {
            key_w = klen;
        }
    }
    if (key_w < 3) {
        key_w = 3;
    }
    size_t key_block_w = key_w + 2;

    int slot_w = cols / (int)n;
    int last_slot_w = cols - slot_w * ((int)n - 1);

    for (size_t i = 0; i < n; i++) {
        int slot_width = (i == n - 1) ? last_slot_w : slot_w;
        const NavItem *item = &row->items[i];

        size_t key_len = strlen(item->key);
        size_t key_pad = key_w > key_len ? key_w - key_len : 0;
        size_t key_left = key_pad / 2;
        size_t key_right = key_pad - key_left;

        StrBuf key_str;
        strbuf_init(&key_str);
        for (size_t k = 0; k < key_left; k++) {
            strbuf_push_char(&key_str, ' ');
        }
        strbuf_push_str(&key_str, item->key);
        for (size_t k = 0; k < key_right; k++) {
            strbuf_push_char(&key_str, ' ');
        }

        char key_block_text[128];
        snprintf(key_block_text, sizeof(key_block_text), " %s ", key_str.data);
        strbuf_free(&key_str);

        ansi_wrap(&sb, key_block_codes(item->color), key_block_text);

        int lab_avail = slot_width - (int)key_block_w - 1;
        if (lab_avail < 0) {
            lab_avail = 0;
        }

        size_t label_len = strlen(item->label);
        char truncated[256];
        if ((int)label_len > lab_avail) {
            int keep = lab_avail - 1;
            if (keep < 0) {
                keep = 0;
            }
            snprintf(truncated, sizeof(truncated), "%.*s\xe2\x80\xa6", keep, item->label);
        } else {
            snprintf(truncated, sizeof(truncated), "%s", item->label);
        }

        int fill = lab_avail - (int)tui_visible_len(truncated);
        if (fill < 0) {
            fill = 0;
        }

        StrBuf label_seg;
        strbuf_init(&label_seg);
        strbuf_push_char(&label_seg, ' ');
        strbuf_push_str(&label_seg, truncated);
        for (int k = 0; k < fill; k++) {
            strbuf_push_char(&label_seg, ' ');
        }

        ansi_wrap(&sb, "40;37", label_seg.data);
        strbuf_free(&label_seg);
    }

    return strbuf_take(&sb);
}

void tui_draw_navbar(const NavRows *rows) {
    int cols = tui_cols();
    StrBuf out;
    strbuf_init(&out);

    if (rows->count == 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "\x1b[1;1H\x1b[2K");
        strbuf_push_str(&out, buf);
        strbuf_push_str(&out, "\x1b[40m");
        for (int i = 0; i < cols; i++) {
            strbuf_push_char(&out, ' ');
        }
        strbuf_push_str(&out, "\x1b[0m");

        snprintf(buf, sizeof(buf), "\x1b[2;1H\x1b[2K");
        strbuf_push_str(&out, buf);
        strbuf_push_str(&out, "\x1b[2m");
        for (int i = 0; i < cols; i++) {
            strbuf_push_str(&out, "\xe2\x94\x80");
        }
        strbuf_push_str(&out, "\x1b[0m");

        for (int r = 3; r <= MAX_NAV_ROWS + 1; r++) {
            snprintf(buf, sizeof(buf), "\x1b[%d;1H\x1b[2K", r);
            strbuf_push_str(&out, buf);
        }

        fputs(out.data, stdout);
        fflush(stdout);
        strbuf_free(&out);
        return;
    }

    for (size_t r = 0; r < rows->count; r++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "\x1b[%zu;1H\x1b[2K", r + 1);
        strbuf_push_str(&out, buf);
        char *row_str = render_nav_row(&rows->rows[r], cols);
        strbuf_push_str(&out, row_str);
        free(row_str);
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%zu;1H\x1b[2K", rows->count + 1);
    strbuf_push_str(&out, buf);
    strbuf_push_str(&out, "\x1b[2m");
    for (int i = 0; i < cols; i++) {
        strbuf_push_str(&out, "\xe2\x94\x80");
    }
    strbuf_push_str(&out, "\x1b[0m");

    for (size_t r = rows->count + 2; r <= (size_t)(MAX_NAV_ROWS + 1); r++) {
        snprintf(buf, sizeof(buf), "\x1b[%zu;1H\x1b[2K", r);
        strbuf_push_str(&out, buf);
    }

    fputs(out.data, stdout);
    fflush(stdout);
    strbuf_free(&out);
}

void tui_draw_bottom_bar(const char *left, const char *right) {
    int cols = tui_cols();
    int row = tui_rows();

    StrBuf ls;
    strbuf_init(&ls);
    if (left[0] != '\0') {
        strbuf_push_str(&ls, "  ");
        strbuf_push_str(&ls, left);
    }

    StrBuf rs;
    strbuf_init(&rs);
    if (right[0] != '\0') {
        strbuf_push_str(&rs, right);
        strbuf_push_str(&rs, "  ");
    }

    int gap = cols - (int)tui_visible_len(ls.data) - (int)tui_visible_len(rs.data);
    if (gap < 0) {
        gap = 0;
    }

    StrBuf out;
    strbuf_init(&out);
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;1H\x1b[2K", row);
    strbuf_push_str(&out, buf);
    ansi_wrap(&out, "2", ls.data);
    for (int i = 0; i < gap; i++) {
        strbuf_push_char(&out, ' ');
    }
    ansi_wrap(&out, "2", rs.data);

    fputs(out.data, stdout);
    fflush(stdout);

    strbuf_free(&ls);
    strbuf_free(&rs);
    strbuf_free(&out);
}

void tui_draw_footer(int total, int scroll_top, int vis, const char *stat_left) {
    int more = total - (scroll_top + vis);
    char right[64] = "";
    if (total > vis) {
        if (more > 0) {
            snprintf(right, sizeof(right), "\xe2\x86\x93 %d more", more);
        } else {
            snprintf(right, sizeof(right), "end");
        }
    }
    tui_draw_bottom_bar(stat_left ? stat_left : "", right);
}

char *tui_kb(const char *s) {
    StrBuf sb;
    strbuf_init(&sb);
    char text[128];
    snprintf(text, sizeof(text), " %s ", s);
    ansi_wrap(&sb, "100;37;1", text);
    return strbuf_take(&sb);
}

void tui_enter_alt(void) {
    fputs("\x1b[?1049h\x1b[?25l", stdout);
    fflush(stdout);
}

void tui_exit_alt(void) {
    fputs("\x1b[?25h\x1b[?1049l\x1b[0m", stdout);
    fflush(stdout);
}

void tui_clear_screen(void) {
    fputs("\x1b[2J", stdout);
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

TuiPopupResult tui_popup_input(const char *label, const char *default_value, int has_input,
                                const char *const *body_lines, size_t body_line_count,
                                TuiRenderBgFn render_bg, TuiPopupChangeFn on_change) {
    char value[4096] = {0};
    if (default_value) {
        snprintf(value, sizeof(value), "%s", default_value);
    }
    size_t cursor = strlen(value);

    typedef enum { FOCUS_INPUT, FOCUS_YES, FOCUS_NO } Focus;
    Focus focus = has_input ? FOCUS_INPUT : FOCUS_YES;

    TuiPopupResult result;
    memset(&result, 0, sizeof(result));

    for (;;) {
        if (render_bg) {
            render_bg();
        }

        int cols = tui_cols();
        int rows = tui_rows();
        int popup_w = cols - 10;
        if (popup_w > 48) {
            popup_w = 48;
        }
        int body_h = (int)body_line_count;
        int popup_h = 8 + body_h;
        int start_y = (rows - popup_h) / 2;
        int start_x = (cols - popup_w) / 2;
        if (start_y < 1) {
            start_y = 1;
        }
        if (start_x < 1) {
            start_x = 1;
        }

        StrBuf out;
        strbuf_init(&out);
        char buf[64];

        StrBuf top;
        strbuf_init(&top);
        strbuf_push_str(&top, "\xe2\x94\x8f");
        for (int i = 0; i < popup_w - 2; i++) {
            strbuf_push_str(&top, "\xe2\x94\x81");
        }
        strbuf_push_str(&top, "\xe2\x94\x93");
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", start_y, start_x);
        strbuf_push_str(&out, buf);
        ansi_wrap(&out, "90", top.data);
        strbuf_free(&top);

        StrBuf mid;
        strbuf_init(&mid);
        for (int i = 0; i < popup_w - 2; i++) {
            strbuf_push_char(&mid, ' ');
        }
        for (int i = 1; i < popup_h - 1; i++) {
            snprintf(buf, sizeof(buf), "\x1b[%d;%dH", start_y + i, start_x);
            strbuf_push_str(&out, buf);
            ansi_wrap(&out, "90", "\xe2\x94\x83");
            strbuf_push_str(&out, mid.data);
            ansi_wrap(&out, "90", "\xe2\x94\x83");
        }
        strbuf_free(&mid);

        StrBuf bot;
        strbuf_init(&bot);
        strbuf_push_str(&bot, "\xe2\x94\x97");
        for (int i = 0; i < popup_w - 2; i++) {
            strbuf_push_str(&bot, "\xe2\x94\x81");
        }
        strbuf_push_str(&bot, "\xe2\x94\x9b");
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", start_y + popup_h - 1, start_x);
        strbuf_push_str(&out, buf);
        ansi_wrap(&out, "90", bot.data);
        strbuf_free(&bot);

        char header[128];
        snprintf(header, sizeof(header), " %s ", label);
        for (size_t i = 0; header[i]; i++) {
            header[i] = (char)toupper((unsigned char)header[i]);
        }
        int header_x = start_x + (popup_w - (int)strlen(header)) / 2;
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", start_y + 1, header_x);
        strbuf_push_str(&out, buf);
        ansi_wrap(&out, "1;37", header);

        StrBuf sep;
        strbuf_init(&sep);
        for (int i = 0; i < popup_w - 4; i++) {
            strbuf_push_str(&sep, "\xe2\x94\x80");
        }
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", start_y + 2, start_x + 2);
        strbuf_push_str(&out, buf);
        ansi_wrap(&out, "2", sep.data);
        strbuf_free(&sep);

        for (size_t i = 0; i < body_line_count; i++) {
            snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (int)(start_y + 3 + i), start_x + 3);
            strbuf_push_str(&out, buf);
            char *padded = tui_pad_or_trim(body_lines[i], popup_w - 6);
            strbuf_push_str(&out, padded);
            free(padded);
        }
        if (body_line_count > 0) {
            StrBuf sep2;
            strbuf_init(&sep2);
            for (int i = 0; i < popup_w - 4; i++) {
                strbuf_push_str(&sep2, "\xe2\x94\x80");
            }
            snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (int)(start_y + 3 + body_line_count), start_x + 2);
            strbuf_push_str(&out, buf);
            ansi_wrap(&out, "2", sep2.data);
            strbuf_free(&sep2);
        }

        int input_w = popup_w - 8;
        int input_y = start_y + 4 + (int)(body_line_count > 0 ? body_line_count + 1 : 0);

        char disp[4096];
        size_t disp_cursor = cursor;
        size_t value_len = strlen(value);

        if ((int)value_len > input_w && input_w > 0) {
            size_t start = 0;
            if (cursor > (size_t)(input_w / 2)) {
                start = cursor - (size_t)(input_w / 2);
            }
            if (start + (size_t)input_w > value_len) {
                start = value_len > (size_t)input_w ? value_len - (size_t)input_w : 0;
            }
            size_t take = value_len - start;
            if (take > (size_t)input_w) {
                take = (size_t)input_w;
            }
            memcpy(disp, value + start, take);
            disp[take] = '\0';
            disp_cursor = cursor - start;

            if (start > 0 && take > 0) {
                disp[0] = '\xe2';
                if (take >= 3) {
                    memmove(disp + 3, disp + 1, take - 1);
                    disp[1] = '\x80';
                    disp[2] = '\xa6';
                    disp[take + 2] = '\0';
                }
            }
            if (start + (size_t)input_w < value_len) {
                size_t dl = strlen(disp);
                if (dl >= 3) {
                    disp[dl - 3] = '\xe2';
                    disp[dl - 2] = '\x80';
                    disp[dl - 1] = '\xa6';
                }
            }
        } else {
            snprintf(disp, sizeof(disp), "%s", value);
        }

        if (has_input) {
            const char *prefix = focus == FOCUS_INPUT ? "\x1b[1;36m> \x1b[0m" : "\x1b[2m> \x1b[0m";
            snprintf(buf, sizeof(buf), "\x1b[%d;%dH", input_y, start_x + 3);
            strbuf_push_str(&out, buf);
            strbuf_push_str(&out, prefix);

            size_t dlen = tui_visible_len(disp);
            StrBuf padded;
            strbuf_init(&padded);
            strbuf_push_str(&padded, disp);
            for (size_t i = dlen; i < (size_t)input_w; i++) {
                strbuf_push_char(&padded, ' ');
            }

            if (focus == FOCUS_INPUT) {
                ansi_wrap(&out, "37", padded.data);
            } else {
                ansi_wrap(&out, "90", padded.data);
            }
            strbuf_free(&padded);
        }

        const char *yes_label = "  Yes  ";
        const char *no_label = "  No  ";
        char yes_buf[32];
        char no_buf[32];
        if (focus == FOCUS_YES) {
            snprintf(yes_buf, sizeof(yes_buf), "\x1b[47;30;1m%s\x1b[0m", yes_label);
        } else {
            snprintf(yes_buf, sizeof(yes_buf), "\x1b[32m%s\x1b[0m", yes_label);
        }
        if (focus == FOCUS_NO) {
            snprintf(no_buf, sizeof(no_buf), "\x1b[47;30;1m%s\x1b[0m", no_label);
        } else {
            snprintf(no_buf, sizeof(no_buf), "\x1b[31m%s\x1b[0m", no_label);
        }

        char footer[128];
        snprintf(footer, sizeof(footer), "%s     %s", yes_buf, no_buf);
        int footer_y = start_y + 6 + (int)(body_line_count > 0 ? body_line_count + 1 : 0);
        int footer_x = start_x + (popup_w - (int)tui_visible_len(footer)) / 2;
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", footer_y, footer_x);
        strbuf_push_str(&out, buf);
        strbuf_push_str(&out, footer);

        if (has_input && focus == FOCUS_INPUT) {
            int cursor_col = start_x + 5 + (int)disp_cursor;
            snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[?25h", input_y, cursor_col);
            strbuf_push_str(&out, buf);
        } else {
            strbuf_push_str(&out, "\x1b[?25l");
        }

        fputs(out.data, stdout);
        fflush(stdout);
        strbuf_free(&out);

        int c = read_raw_byte();
        if (c < 0) {
            continue;
        }

        if (c == 3) {
            result.confirmed = 0;
            return result;
        }

        if (focus == FOCUS_INPUT) {
            if (c == '\r' || c == '\n') {
                result.confirmed = 1;
                snprintf(result.value, sizeof(result.value), "%s", value);
                return result;
            }
            if (c == 0x1b) {
                int c2 = read_raw_byte();
                if (c2 == '[') {
                    int c3 = read_raw_byte();
                    if (c3 == 'B') {
                        focus = FOCUS_YES;
                        continue;
                    }
                    if (c3 == 'C') {
                        if (cursor < strlen(value)) {
                            cursor++;
                        }
                        continue;
                    }
                    if (c3 == 'D') {
                        if (cursor > 0) {
                            cursor--;
                        }
                        continue;
                    }
                    if (c3 == 'H') {
                        cursor = 0;
                        continue;
                    }
                    if (c3 == 'F') {
                        cursor = strlen(value);
                        continue;
                    }
                    if (c3 == '3') {
                        int c4 = read_raw_byte();
                        (void)c4;
                        if (cursor < strlen(value)) {
                            memmove(value + cursor, value + cursor + 1, strlen(value) - cursor);
                            if (on_change) {
                                on_change(value);
                            }
                        }
                        continue;
                    }
                    continue;
                }
                result.confirmed = 0;
                return result;
            }
            if (c == '\t') {
                focus = FOCUS_YES;
                continue;
            }
            if (c == 127 || c == 8) {
                if (cursor > 0) {
                    memmove(value + cursor - 1, value + cursor, strlen(value) - cursor + 1);
                    cursor--;
                    if (on_change) {
                        on_change(value);
                    }
                }
                continue;
            }
            if (c >= 32 && c < 127) {
                size_t len = strlen(value);
                if (len + 1 < sizeof(value)) {
                    memmove(value + cursor + 1, value + cursor, len - cursor + 1);
                    value[cursor] = (char)c;
                    cursor++;
                    if (on_change) {
                        on_change(value);
                    }
                }
                continue;
            }
        } else {
            if (c == '\r' || c == '\n') {
                if (focus == FOCUS_YES) {
                    result.confirmed = 1;
                    snprintf(result.value, sizeof(result.value), "%s", value);
                } else {
                    result.confirmed = 0;
                }
                return result;
            }
            if (c == '\t') {
                if (default_value == NULL) {
                    focus = (focus == FOCUS_YES) ? FOCUS_NO : FOCUS_YES;
                } else {
                    focus = (focus == FOCUS_YES) ? FOCUS_NO : FOCUS_INPUT;
                }
                continue;
            }
            if (c == 0x1b) {
                int c2 = read_raw_byte();
                if (c2 == '[') {
                    int c3 = read_raw_byte();
                    if (c3 == 'C') {
                        focus = FOCUS_NO;
                        continue;
                    }
                    if (c3 == 'D') {
                        focus = FOCUS_YES;
                        continue;
                    }
                    if (c3 == 'A' && has_input) {
                        focus = FOCUS_INPUT;
                        continue;
                    }
                    continue;
                }
                result.confirmed = 0;
                return result;
            }
            if (c == 'y' || c == 'Y') {
                result.confirmed = 1;
                snprintf(result.value, sizeof(result.value), "%s", value);
                return result;
            }
            if (c == 'n' || c == 'N') {
                result.confirmed = 0;
                return result;
            }
        }
    }
}
