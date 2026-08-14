#ifndef FSH_PREVIEW_PREVIEW_H
#define FSH_PREVIEW_PREVIEW_H

#include <stddef.h>

typedef enum { PREVIEW_TEXT, PREVIEW_BINARY, PREVIEW_DIR, PREVIEW_IMAGE, PREVIEW_UNSUPPORTED, PREVIEW_EMPTY } PreviewKind;

typedef struct {
    char size[32];
    char modified[32];
    char perms[16];
    char ext[32];
} FileMeta;

typedef struct {
    int total_items;
    int dirs;
    int files;
    char size_str[32];
} DirMeta;

typedef struct {
    char name[1024];
    int is_dir;
} PreviewDirEntry;

typedef struct {
    PreviewKind kind;
    FileMeta meta;
    DirMeta dir_meta;
    char **lines;
    size_t line_count;
    PreviewDirEntry *entries;
    size_t entry_count;
    int image_width;
    int image_height;
} PreviewContent;

typedef enum { PREVIEW_MODE_SPLIT, PREVIEW_MODE_OVERLAY } PreviewMode;
typedef enum { PREVIEW_PREF_AUTO, PREVIEW_PREF_SPLIT, PREVIEW_PREF_OVERLAY } PreviewPref;

#define SPLIT_THRESHOLD 110
#define PREVIEW_RATIO 0.4
#define OVERLAY_LINES 12

PreviewMode preview_get_mode(PreviewPref pref);
int preview_cols(void);
int preview_list_cols(void);

typedef struct {
    int scroll_top;
    int scroll_left;
    int cursor_row;
    int cursor_col;
    int is_preview_mode;
    char path[4096];
    PreviewContent *content;
} PreviewState;

void preview_state_init(PreviewState *state);
void preview_update(PreviewState *state, const char *full_path);
void preview_force_update(PreviewState *state, const char *full_path);
void preview_free_content(PreviewContent *content);
void preview_move_cursor(PreviewState *state, int d_row, int d_col, int vis_h, int body_w);

void preview_draw_split(PreviewState *state, int nav_rows, int list_w);
void preview_draw_overlay(PreviewState *state, int nav_rows);

const char *preview_line_color_hex(const char *ext);

#endif
