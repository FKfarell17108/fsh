#ifndef FSH_TUI_TUI_H
#define FSH_TUI_TUI_H

#include <stddef.h>

typedef enum { NAV_DEFAULT, NAV_GREEN, NAV_YELLOW, NAV_RED, NAV_CYAN } NavItemColor;

typedef struct {
    char key[16];
    char label[64];
    NavItemColor color;
} NavItem;

typedef struct {
    NavItem *items;
    size_t count;
    size_t capacity;
} NavRow;

typedef struct {
    NavRow *rows;
    size_t count;
    size_t capacity;
} NavRows;

void navrows_init(NavRows *nr);
size_t navrows_add_row(NavRows *nr);
void navrows_add_item(NavRows *nr, size_t row_index, const char *key, const char *label, NavItemColor color);
void navrows_free(NavRows *nr);

int tui_cols(void);
int tui_rows(void);

int tui_read_byte(void);
int tui_read_byte_after_esc(void);

size_t tui_visible_len(const char *str);
char *tui_pad_or_trim(const char *str, int width);

void tui_draw_navbar(const NavRows *rows);
void tui_draw_bottom_bar(const char *left, const char *right);
void tui_draw_footer(int total, int scroll_top, int vis, const char *stat_left);

char *tui_kb(const char *s);

void tui_enter_alt(void);
void tui_exit_alt(void);
void tui_clear_screen(void);

typedef void (*TuiRenderBgFn)(void);
typedef void (*TuiPopupChangeFn)(const char *value);

typedef struct {
    int confirmed;
    char value[4096];
} TuiPopupResult;

TuiPopupResult tui_popup_input(const char *label, const char *default_value, int has_input,
                                const char *const *body_lines, size_t body_line_count,
                                TuiRenderBgFn render_bg, TuiPopupChangeFn on_change);

#endif
