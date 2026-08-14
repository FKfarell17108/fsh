#include "preview/preview.h"

#include "tui/tui.h"
#include "util/strbuf.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *IMAGE_EXTS[] = {"png", "jpg", "jpeg", "gif", "bmp", "webp", "ico", "tiff", "tif"};
#define IMAGE_EXTS_COUNT (sizeof(IMAGE_EXTS) / sizeof(IMAGE_EXTS[0]))

static int is_image_ext(const char *ext) {
    for (size_t i = 0; i < IMAGE_EXTS_COUNT; i++) {
        if (strcasecmp(ext, IMAGE_EXTS[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

PreviewMode preview_get_mode(PreviewPref pref) {
    if (pref == PREVIEW_PREF_SPLIT) {
        return PREVIEW_MODE_SPLIT;
    }
    if (pref == PREVIEW_PREF_OVERLAY) {
        return PREVIEW_MODE_OVERLAY;
    }
    return tui_cols() >= SPLIT_THRESHOLD ? PREVIEW_MODE_SPLIT : PREVIEW_MODE_OVERLAY;
}

int preview_cols(void) {
    return (int)((double)tui_cols() * PREVIEW_RATIO);
}

int preview_list_cols(void) {
    return tui_cols() - preview_cols() - 1;
}

static void fmt_size(long long bytes, char *out, size_t out_size) {
    if (bytes < 1024) {
        snprintf(out, out_size, "%lld B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(out, out_size, "%.1f KB", (double)bytes / 1024.0);
    } else if (bytes < 1024LL * 1024 * 1024) {
        snprintf(out, out_size, "%.1f MB", (double)bytes / 1024.0 / 1024.0);
    } else {
        snprintf(out, out_size, "%.2f GB", (double)bytes / 1024.0 / 1024.0 / 1024.0);
    }
}

static void fmt_perms(int mode, char *out, size_t out_size) {
    static const char *table[] = {"---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx"};
    snprintf(out, out_size, "%s%s%s", table[(mode >> 6) & 7], table[(mode >> 3) & 7], table[mode & 7]);
}

static void fmt_date(long long mtime_sec, char *out, size_t out_size) {
    time_t t = (time_t)mtime_sec;
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(out, out_size, "%b %e, %H:%M", &tmv);
}

static int is_binary_buf(const unsigned char *buf, size_t len) {
    size_t check = len < 512 ? len : 512;
    for (size_t i = 0; i < check; i++) {
        unsigned char b = buf[i];
        if (b == 0) {
            return 1;
        }
        if (b < 8 || (b > 13 && b < 32 && b != 27)) {
            return 1;
        }
    }
    return 0;
}

static int get_image_dimensions(const unsigned char *buf, size_t len, int *width, int *height) {
    if (len < 24) {
        return 0;
    }

    if (buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4e && buf[3] == 0x47) {
        int w = (buf[16] << 24) | (buf[17] << 16) | (buf[18] << 8) | buf[19];
        int h = (buf[20] << 24) | (buf[21] << 16) | (buf[22] << 8) | buf[23];
        if (w > 0 && h > 0 && w < 10000 && h < 10000) {
            *width = w;
            *height = h;
            return 1;
        }
    }

    if (buf[0] == 0xff && buf[1] == 0xd8 && buf[2] == 0xff) {
        size_t i = 3;
        while (i + 1 < len) {
            if (buf[i] == 0xff && buf[i + 1] >= 0xc0 && buf[i + 1] <= 0xc3) {
                if (i + 8 < len) {
                    int h = (buf[i + 5] << 8) | buf[i + 6];
                    int w = (buf[i + 7] << 8) | buf[i + 8];
                    if (w > 0 && h > 0 && w < 10000 && h < 10000) {
                        *width = w;
                        *height = h;
                        return 1;
                    }
                }
                break;
            }
            if (buf[i] != 0xff) {
                i++;
                continue;
            }
            if (i + 3 >= len) {
                break;
            }
            int seg_len = (buf[i + 2] << 8) | buf[i + 3];
            if (seg_len < 2) {
                break;
            }
            i += 2 + (size_t)seg_len;
        }
    }

    if (buf[0] == 0x47 && buf[1] == 0x49 && buf[2] == 0x46) {
        int w = buf[6] | (buf[7] << 8);
        int h = buf[8] | (buf[9] << 8);
        if (w > 0 && h > 0 && w < 10000 && h < 10000) {
            *width = w;
            *height = h;
            return 1;
        }
    }

    if (buf[0] == 0x42 && buf[1] == 0x4d) {
        int w = buf[18] | (buf[19] << 8) | (buf[20] << 16) | (buf[21] << 24);
        int h = buf[22] | (buf[23] << 8) | (buf[24] << 16) | (buf[25] << 24);
        h = h < 0 ? -h : h;
        if (w > 0 && h > 0 && w < 10000 && h < 10000) {
            *width = w;
            *height = h;
            return 1;
        }
    }

    return 0;
}

static long long dir_size(const char *dir_path, int depth) {
    if (depth > 3) {
        return 0;
    }
    long long total = 0;
    DIR *dp = opendir(dir_path);
    if (!dp) {
        return 0;
    }
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", dir_path, entry->d_name);
        struct stat st;
        if (lstat(full, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            total += dir_size(full, depth + 1);
        } else {
            total += st.st_size;
        }
    }
    closedir(dp);
    return total;
}

void preview_state_init(PreviewState *state) {
    memset(state, 0, sizeof(*state));
}

void preview_free_content(PreviewContent *content) {
    if (!content) {
        return;
    }
    for (size_t i = 0; i < content->line_count; i++) {
        free(content->lines[i]);
    }
    free(content->lines);
    free(content->entries);
    free(content);
}

static PreviewContent *build_preview(const char *full_path) {
    PreviewContent *content = calloc(1, sizeof(PreviewContent));

    struct stat st;
    if (stat(full_path, &st) != 0) {
        content->kind = PREVIEW_EMPTY;
        return content;
    }

    if (S_ISDIR(st.st_mode)) {
        content->kind = PREVIEW_DIR;
        DIR *dp = opendir(full_path);
        int dirs = 0;
        int files = 0;
        PreviewDirEntry *entries = NULL;
        size_t count = 0;
        size_t capacity = 0;
        if (dp) {
            struct dirent *entry;
            while ((entry = readdir(dp)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                char full[4096];
                snprintf(full, sizeof(full), "%s/%s", full_path, entry->d_name);
                struct stat est;
                int is_dir = 0;
                if (lstat(full, &est) == 0) {
                    is_dir = S_ISDIR(est.st_mode);
                }
                if (is_dir) {
                    dirs++;
                } else {
                    files++;
                }
                if (count == capacity) {
                    capacity = capacity ? capacity * 2 : 32;
                    entries = realloc(entries, capacity * sizeof(PreviewDirEntry));
                }
                snprintf(entries[count].name, sizeof(entries[count].name), "%s", entry->d_name);
                entries[count].is_dir = is_dir;
                count++;
            }
            closedir(dp);
        }

        for (size_t i = 0; i < count; i++) {
            for (size_t j = i + 1; j < count; j++) {
                int cmp = (entries[j].is_dir ? 1 : 0) - (entries[i].is_dir ? 1 : 0);
                if (cmp > 0 || (cmp == 0 && strcmp(entries[j].name, entries[i].name) < 0)) {
                    PreviewDirEntry tmp = entries[i];
                    entries[i] = entries[j];
                    entries[j] = tmp;
                }
            }
        }

        content->entries = entries;
        content->entry_count = count;
        content->dir_meta.dirs = dirs;
        content->dir_meta.files = files;
        content->dir_meta.total_items = dirs + files;
        fmt_size(dir_size(full_path, 0), content->dir_meta.size_str, sizeof(content->dir_meta.size_str));
        return content;
    }

    fmt_size(st.st_size, content->meta.size, sizeof(content->meta.size));
    fmt_date(st.st_mtime, content->meta.modified, sizeof(content->meta.modified));
    fmt_perms((int)(st.st_mode & 0777), content->meta.perms, sizeof(content->meta.perms));

    const char *dot = strrchr(full_path, '.');
    const char *slash = strrchr(full_path, '/');
    if (dot && (!slash || dot > slash) && dot[1] != '\0') {
        snprintf(content->meta.ext, sizeof(content->meta.ext), "%s", dot + 1);
        for (char *p = content->meta.ext; *p; p++) {
            *p = (char)tolower((unsigned char)*p);
        }
    }

    if (st.st_size == 0) {
        content->kind = PREVIEW_TEXT;
        content->lines = malloc(sizeof(char *));
        content->lines[0] = strdup("");
        content->line_count = 1;
        return content;
    }

    if (st.st_size > 10 * 1024 * 1024) {
        content->kind = PREVIEW_BINARY;
        return content;
    }

    FILE *f = fopen(full_path, "rb");
    if (!f) {
        content->kind = PREVIEW_BINARY;
        return content;
    }
    unsigned char *buf = malloc((size_t)st.st_size);
    size_t read_n = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);

    if (is_image_ext(content->meta.ext)) {
        int w = 0;
        int h = 0;
        if (get_image_dimensions(buf, read_n, &w, &h)) {
            content->kind = PREVIEW_IMAGE;
            content->image_width = w;
            content->image_height = h;
            free(buf);
            return content;
        }
    }

    if (is_binary_buf(buf, read_n)) {
        content->kind = PREVIEW_BINARY;
        free(buf);
        return content;
    }

    content->kind = PREVIEW_TEXT;
    char **lines = NULL;
    size_t line_count = 0;
    size_t capacity = 0;
    size_t start = 0;
    for (size_t i = 0; i <= read_n; i++) {
        if (i == read_n || buf[i] == '\n') {
            size_t len = i - start;
            if (line_count == capacity) {
                capacity = capacity ? capacity * 2 : 64;
                lines = realloc(lines, capacity * sizeof(char *));
            }
            char *line = malloc(len + 1);
            memcpy(line, buf + start, len);
            line[len] = '\0';
            if (len > 0 && line[len - 1] == '\r') {
                line[len - 1] = '\0';
            }
            lines[line_count++] = line;
            start = i + 1;
        }
    }
    content->lines = lines;
    content->line_count = line_count;
    free(buf);
    return content;
}

void preview_update(PreviewState *state, const char *full_path) {
    if (strcmp(state->path, full_path) == 0) {
        return;
    }
    preview_force_update(state, full_path);
}

void preview_force_update(PreviewState *state, const char *full_path) {
    preview_free_content(state->content);
    snprintf(state->path, sizeof(state->path), "%s", full_path);
    state->content = build_preview(full_path);
    state->scroll_top = 0;
    state->scroll_left = 0;
    state->cursor_row = 0;
    state->cursor_col = 0;
}

void preview_move_cursor(PreviewState *state, int d_row, int d_col, int vis_h, int body_w) {
    if (!state->content) {
        return;
    }
    if (state->content->kind != PREVIEW_TEXT) {
        if (d_row != 0) {
            state->scroll_top = state->scroll_top + d_row < 0 ? 0 : state->scroll_top + d_row;
        }
        return;
    }

    size_t nlines = state->content->line_count;

    if (d_col != 0) {
        int line_len = state->cursor_row < (int)nlines ? (int)strlen(state->content->lines[state->cursor_row]) : 0;
        if (d_col > 0) {
            if (state->cursor_col + d_col > line_len) {
                if (state->cursor_row < (int)nlines - 1) {
                    state->cursor_row++;
                    state->cursor_col = 0;
                } else {
                    state->cursor_col = line_len;
                }
            } else {
                state->cursor_col += d_col;
            }
        } else {
            if (state->cursor_col + d_col < 0) {
                if (state->cursor_row > 0) {
                    state->cursor_row--;
                    state->cursor_col = (int)strlen(state->content->lines[state->cursor_row]);
                } else {
                    state->cursor_col = 0;
                }
            } else {
                state->cursor_col += d_col;
            }
        }
    }

    if (d_row != 0) {
        state->cursor_row += d_row;
        if (state->cursor_row < 0) {
            state->cursor_row = 0;
        }
        if (state->cursor_row > (int)nlines - 1) {
            state->cursor_row = (int)nlines - 1;
        }
        int max_len = state->cursor_row >= 0 ? (int)strlen(state->content->lines[state->cursor_row]) : 0;
        if (state->cursor_col > max_len) {
            state->cursor_col = max_len;
        }
    }

    if (state->cursor_row < state->scroll_top) {
        state->scroll_top = state->cursor_row;
    } else if (state->cursor_row >= state->scroll_top + vis_h) {
        state->scroll_top = state->cursor_row - vis_h + 1;
    }

    if (state->cursor_col < state->scroll_left) {
        state->scroll_left = state->cursor_col;
    } else if (state->cursor_col >= state->scroll_left + body_w) {
        state->scroll_left = state->cursor_col - body_w + 1;
    }
}

typedef struct {
    const char *ext;
    const char *hex;
} ExtColor;

static const ExtColor TEXT_COLORS[] = {
    {"ts", "#6EC6BF"}, {"js", "#FFD580"}, {"tsx", "#6EC6BF"}, {"jsx", "#FFD580"},
    {"json", "#AEDD87"}, {"md", "#D4A9F5"}, {"py", "#FFD580"},
    {"sh", "#AEDD87"}, {"bash", "#AEDD87"}, {"css", "#70D4FF"},
    {"html", "#FFA878"}, {"xml", "#FFA878"}, {"yml", "#FF9E64"},
    {"yaml", "#FF9E64"}, {"toml", "#FF9E64"}, {"env", "#FF9E64"},
    {"rs", "#FFA878"}, {"go", "#70D4FF"}, {"java", "#F5C542"},
    {"c", "#B0B8D8"}, {"cpp", "#B0B8D8"}, {"h", "#B0B8D8"},
    {"rb", "#FF7B8A"}, {"php", "#D4A9F5"}, {"sql", "#5BC8F5"},
    {"log", "#888FA8"}, {"txt", "#FFFFFF"},
};
#define TEXT_COLORS_COUNT (sizeof(TEXT_COLORS) / sizeof(TEXT_COLORS[0]))

const char *preview_line_color_hex(const char *ext) {
    for (size_t i = 0; i < TEXT_COLORS_COUNT; i++) {
        if (strcmp(TEXT_COLORS[i].ext, ext) == 0) {
            return TEXT_COLORS[i].hex;
        }
    }
    return "#FFFFFF";
}

static void append_hex_color(StrBuf *sb, const char *hex, int dim) {
    unsigned int r;
    unsigned int g;
    unsigned int b;
    sscanf(hex, "#%02x%02x%02x", &r, &g, &b);
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[38;2;%u;%u;%um", r, g, b);
    strbuf_push_str(sb, buf);
    if (dim) {
        strbuf_push_str(sb, "\x1b[2m");
    }
}

static char *pad_line(const char *l, int width) {
    size_t vl = tui_visible_len(l);
    if ((int)vl > width) {
        char *trimmed = tui_pad_or_trim(l, width);
        return trimmed;
    }
    if ((int)vl < width) {
        StrBuf sb;
        strbuf_init(&sb);
        strbuf_push_str(&sb, l);
        for (int i = (int)vl; i < width; i++) {
            strbuf_push_char(&sb, ' ');
        }
        return strbuf_take(&sb);
    }
    return strdup(l);
}

typedef struct {
    char **header;
    size_t header_count;
    char **body;
    size_t body_count;
} RenderedPreview;

static void rp_push_header(RenderedPreview *rp, char *line) {
    rp->header = realloc(rp->header, (rp->header_count + 1) * sizeof(char *));
    rp->header[rp->header_count++] = line;
}

static void rp_push_body(RenderedPreview *rp, char *line) {
    rp->body = realloc(rp->body, (rp->body_count + 1) * sizeof(char *));
    rp->body[rp->body_count++] = line;
}

static void render_meta_lines(RenderedPreview *rp, const FileMeta *meta, int width, const char *full_path) {
    char line[4200];
    StrBuf dash;
    strbuf_init(&dash);
    for (int i = 0; i < width; i++) {
        strbuf_push_char(&dash, '-');
    }
    char *l0 = pad_line(dash.data, width);
    rp_push_header(rp, l0);
    strbuf_free(&dash);

    snprintf(line, sizeof(line), "  size      %s", meta->size);
    rp_push_header(rp, pad_line(line, width));
    snprintf(line, sizeof(line), "  modified  %s", meta->modified);
    rp_push_header(rp, pad_line(line, width));

    const char *home = getenv("HOME");
    char display_path[4096];
    if (home && strncmp(full_path, home, strlen(home)) == 0) {
        snprintf(display_path, sizeof(display_path), "~%s", full_path + strlen(home));
    } else {
        snprintf(display_path, sizeof(display_path), "%s", full_path);
    }
    snprintf(line, sizeof(line), "  path      %s", display_path);
    rp_push_header(rp, pad_line(line, width));

    if (meta->ext[0] != '\0') {
        snprintf(line, sizeof(line), "  type      %s", meta->ext);
        rp_push_header(rp, pad_line(line, width));
    }

    StrBuf dash2;
    strbuf_init(&dash2);
    for (int i = 0; i < width; i++) {
        strbuf_push_char(&dash2, '-');
    }
    rp_push_header(rp, pad_line(dash2.data, width));
    strbuf_free(&dash2);
}

static void render_dir_meta_lines(RenderedPreview *rp, const DirMeta *meta, int width) {
    char line[128];
    StrBuf dash;
    strbuf_init(&dash);
    for (int i = 0; i < width; i++) {
        strbuf_push_char(&dash, '-');
    }
    rp_push_header(rp, pad_line(dash.data, width));

    snprintf(line, sizeof(line), "  items     %d", meta->total_items);
    rp_push_header(rp, pad_line(line, width));
    snprintf(line, sizeof(line), "  dirs      %d", meta->dirs);
    rp_push_header(rp, pad_line(line, width));
    snprintf(line, sizeof(line), "  files     %d", meta->files);
    rp_push_header(rp, pad_line(line, width));
    snprintf(line, sizeof(line), "  size      %s", meta->size_str);
    rp_push_header(rp, pad_line(line, width));

    rp_push_header(rp, pad_line(dash.data, width));
    strbuf_free(&dash);
}

static const char *basename_of(const char *p) {
    const char *slash = strrchr(p, '/');
    return slash ? slash + 1 : p;
}

static RenderedPreview render_content(const PreviewContent *content, int width, const char *full_path,
                                       const PreviewState *state) {
    RenderedPreview rp;
    memset(&rp, 0, sizeof(rp));

    if (content->kind == PREVIEW_EMPTY) {
        rp_push_body(&rp, pad_line("  (nothing here)", width));
        return rp;
    }

    if (content->kind == PREVIEW_BINARY) {
        rp_push_header(&rp, pad_line("  binary file", width));
        render_meta_lines(&rp, &content->meta, width, full_path);
        return rp;
    }

    if (content->kind == PREVIEW_DIR) {
        char line[1200];
        snprintf(line, sizeof(line), "  %s/", basename_of(full_path));
        rp_push_header(&rp, pad_line(line, width));
        render_dir_meta_lines(&rp, &content->dir_meta, width);

        if (content->entry_count == 0) {
            rp_push_body(&rp, pad_line("  (empty directory)", width));
        } else {
            for (size_t i = 0; i < content->entry_count; i++) {
                const PreviewDirEntry *e = &content->entries[i];
                char bl[1200];
                snprintf(bl, sizeof(bl), "  %s%s", e->is_dir ? "\xe2\x96\xb8 " : "  ", e->name);
                rp_push_body(&rp, pad_line(bl, width));
            }
        }
        return rp;
    }

    if (content->kind == PREVIEW_IMAGE) {
        char line[1200];
        snprintf(line, sizeof(line), "  %s", basename_of(full_path));
        rp_push_header(&rp, pad_line(line, width));
        render_meta_lines(&rp, &content->meta, width, full_path);
        snprintf(line, sizeof(line), "  %dx%d (preview belum didukung)", content->image_width, content->image_height);
        rp_push_header(&rp, pad_line(line, width));
        return rp;
    }

    char line[1200];
    snprintf(line, sizeof(line), "  %s", basename_of(full_path));
    rp_push_header(&rp, pad_line(line, width));
    render_meta_lines(&rp, &content->meta, width, full_path);

    if (content->line_count == 0 || (content->line_count == 1 && content->lines[0][0] == '\0')) {
        rp_push_body(&rp, pad_line("  (empty file)", width));
        return rp;
    }

    int num_w = 5;
    int body_w = width - num_w > 1 ? width - num_w : 1;
    const char *hex = preview_line_color_hex(content->meta.ext);

    for (size_t i = 0; i < content->line_count; i++) {
        char num[16];
        snprintf(num, sizeof(num), "%4zu ", i + 1);

        const char *raw = content->lines[i];
        int sl = (int)strlen(raw);
        int scroll_left = state->scroll_left;

        char sliced[4096];
        int copy_len = sl - scroll_left;
        if (copy_len < 0) {
            copy_len = 0;
        }
        if (copy_len > body_w) {
            copy_len = body_w;
        }
        if (copy_len > 0) {
            memcpy(sliced, raw + scroll_left, (size_t)copy_len);
        }
        for (int k = copy_len; k < body_w; k++) {
            sliced[k] = ' ';
        }
        sliced[body_w] = '\0';

        StrBuf out;
        strbuf_init(&out);
        strbuf_push_str(&out, "\x1b[2m");
        strbuf_push_str(&out, num);
        strbuf_push_str(&out, "\x1b[0m");

        if (state->is_preview_mode && (int)i == state->cursor_row) {
            int c = state->cursor_col - scroll_left;
            if (c >= 0 && c < body_w) {
                char left[4096];
                char right[4096];
                memcpy(left, sliced, (size_t)c);
                left[c] = '\0';
                snprintf(right, sizeof(right), "%s", sliced + c + 1);

                append_hex_color(&out, hex, 0);
                strbuf_push_str(&out, left);
                strbuf_push_str(&out, "\x1b[0m\x1b[47;30m");
                strbuf_push_char(&out, sliced[c]);
                strbuf_push_str(&out, "\x1b[0m");
                append_hex_color(&out, hex, 0);
                strbuf_push_str(&out, right);
                strbuf_push_str(&out, "\x1b[0m");
            } else {
                append_hex_color(&out, hex, 0);
                strbuf_push_str(&out, sliced);
                strbuf_push_str(&out, "\x1b[0m");
            }
        } else {
            append_hex_color(&out, hex, 0);
            strbuf_push_str(&out, sliced);
            strbuf_push_str(&out, "\x1b[0m");
        }

        rp_push_body(&rp, strbuf_take(&out));
    }

    return rp;
}

static void rendered_free(RenderedPreview *rp) {
    for (size_t i = 0; i < rp->header_count; i++) {
        free(rp->header[i]);
    }
    for (size_t i = 0; i < rp->body_count; i++) {
        free(rp->body[i]);
    }
    free(rp->header);
    free(rp->body);
}

void preview_draw_split(PreviewState *state, int nav_rows, int list_w) {
    int cols = tui_cols();
    int pv_w = cols - list_w - 1;
    int start_r = nav_rows + 2;
    int end_r = tui_rows() - 1;
    int vis_h = end_r - start_r + 1;
    if (vis_h < 1) {
        vis_h = 1;
    }
    int div_col = list_w + 1;

    RenderedPreview rp;
    if (!state->content) {
        memset(&rp, 0, sizeof(rp));
        rp_push_body(&rp, pad_line("  no preview", pv_w));
    } else {
        rp = render_content(state->content, pv_w, state->path, state);
    }

    int body_vis_h = vis_h - (int)rp.header_count;
    if (body_vis_h < 0) {
        body_vis_h = 0;
    }
    int max_scroll = (int)rp.body_count - body_vis_h;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (state->scroll_top > max_scroll) {
        state->scroll_top = max_scroll;
    }
    if (state->scroll_top < 0) {
        state->scroll_top = 0;
    }

    StrBuf out;
    strbuf_init(&out);
    char buf[64];
    int r = 0;
    for (size_t i = 0; i < rp.header_count && r < vis_h; i++) {
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", start_r + r, div_col);
        strbuf_push_str(&out, buf);
        strbuf_push_str(&out, "\x1b[2m\xe2\x94\x82\x1b[0m");
        strbuf_push_str(&out, rp.header[i]);
        r++;
    }
    for (int i = 0; r < vis_h; i++) {
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", start_r + r, div_col);
        strbuf_push_str(&out, buf);
        strbuf_push_str(&out, "\x1b[2m\xe2\x94\x82\x1b[0m");
        size_t idx = (size_t)(state->scroll_top + i);
        if (idx < rp.body_count) {
            strbuf_push_str(&out, rp.body[idx]);
        } else {
            for (int k = 0; k < pv_w; k++) {
                strbuf_push_char(&out, ' ');
            }
        }
        r++;
    }

    fputs(out.data, stdout);
    strbuf_free(&out);
    rendered_free(&rp);
}

void preview_draw_overlay(PreviewState *state, int nav_rows) {
    (void)nav_rows;
    int cols = tui_cols();
    int start_r = tui_rows() - OVERLAY_LINES - 1;
    int vis_h = OVERLAY_LINES;

    RenderedPreview rp;
    if (!state->content) {
        memset(&rp, 0, sizeof(rp));
        rp_push_body(&rp, pad_line("  no preview", cols - 2));
    } else {
        rp = render_content(state->content, cols - 2, state->path, state);
    }

    int body_vis_h = vis_h - (int)rp.header_count;
    if (body_vis_h < 0) {
        body_vis_h = 0;
    }
    int max_scroll = (int)rp.body_count - body_vis_h;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (state->scroll_top > max_scroll) {
        state->scroll_top = max_scroll;
    }
    if (state->scroll_top < 0) {
        state->scroll_top = 0;
    }

    StrBuf out;
    strbuf_init(&out);
    char buf[64];
    snprintf(buf, sizeof(buf), "\x1b[%d;1H\x1b[2m", start_r);
    strbuf_push_str(&out, buf);
    for (int i = 0; i < cols; i++) {
        strbuf_push_str(&out, "\xe2\x94\x80");
    }
    strbuf_push_str(&out, "\x1b[0m");

    int r = 0;
    for (size_t i = 0; i < rp.header_count && r < vis_h; i++) {
        snprintf(buf, sizeof(buf), "\x1b[%d;1H ", start_r + 1 + r);
        strbuf_push_str(&out, buf);
        strbuf_push_str(&out, rp.header[i]);
        strbuf_push_char(&out, ' ');
        r++;
    }
    for (int i = 0; r < vis_h; i++) {
        snprintf(buf, sizeof(buf), "\x1b[%d;1H ", start_r + 1 + r);
        strbuf_push_str(&out, buf);
        size_t idx = (size_t)(state->scroll_top + i);
        if (idx < rp.body_count) {
            strbuf_push_str(&out, rp.body[idx]);
        } else {
            for (int k = 0; k < cols - 2; k++) {
                strbuf_push_char(&out, ' ');
            }
        }
        strbuf_push_char(&out, ' ');
        r++;
    }

    fputs(out.data, stdout);
    strbuf_free(&out);
    rendered_free(&rp);
}
