#include "input/line_editor.h"

#include "highlight/highlight.h"
#include "input/history.h"
#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    char *buf;
    size_t len;
    size_t capacity;
    size_t cursor;
} LineBuf;

static void linebuf_init(LineBuf *lb) {
    lb->capacity = 128;
    lb->buf = malloc(lb->capacity);
    lb->buf[0] = '\0';
    lb->len = 0;
    lb->cursor = 0;
}

static void linebuf_ensure(LineBuf *lb, size_t extra) {
    if (lb->len + extra + 1 <= lb->capacity) {
        return;
    }
    while (lb->len + extra + 1 > lb->capacity) {
        lb->capacity *= 2;
    }
    lb->buf = realloc(lb->buf, lb->capacity);
}

static void linebuf_insert(LineBuf *lb, char c) {
    linebuf_ensure(lb, 1);
    memmove(lb->buf + lb->cursor + 1, lb->buf + lb->cursor, lb->len - lb->cursor + 1);
    lb->buf[lb->cursor] = c;
    lb->cursor++;
    lb->len++;
}

static void linebuf_delete_before_cursor(LineBuf *lb) {
    if (lb->cursor == 0) {
        return;
    }
    memmove(lb->buf + lb->cursor - 1, lb->buf + lb->cursor, lb->len - lb->cursor + 1);
    lb->cursor--;
    lb->len--;
}

static void linebuf_delete_at_cursor(LineBuf *lb) {
    if (lb->cursor >= lb->len) {
        return;
    }
    memmove(lb->buf + lb->cursor, lb->buf + lb->cursor + 1, lb->len - lb->cursor);
    lb->len--;
}

static void linebuf_set(LineBuf *lb, const char *text) {
    size_t tlen = strlen(text);
    if (tlen > lb->len) {
        linebuf_ensure(lb, tlen - lb->len);
    }
    memcpy(lb->buf, text, tlen + 1);
    lb->len = tlen;
    lb->cursor = tlen;
}

static void redraw(const char *prompt, LineBuf *lb) {
    char *highlighted = highlight_render(lb->buf);
    printf("\r\x1b[K%s%s", prompt, highlighted);
    free(highlighted);
    size_t tail = lb->len - lb->cursor;
    if (tail > 0) {
        printf("\x1b[%zuD", tail);
    }
    fflush(stdout);
}

static char *read_line_non_tty(const char *prompt) {
    printf("%s", prompt);
    fflush(stdout);

    size_t cap = 0;
    char *line = NULL;
    ssize_t n = getline(&line, &cap, stdin);
    if (n < 0) {
        free(line);
        return NULL;
    }
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
        line[--n] = '\0';
    }
    return line;
}

char *line_editor_read(const char *prompt) {
    if (!isatty(STDIN_FILENO)) {
        return read_line_non_tty(prompt);
    }

    platform_raw_mode_enable();

    LineBuf lb;
    linebuf_init(&lb);
    long history_pos = -1;
    char saved_line[4096] = {0};

    printf("%s", prompt);
    fflush(stdout);

    int eof = 0;

    for (;;) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            continue;
        }

        if (c == '\r' || c == '\n') {
            printf("\r\n");
            break;
        }

        if (c == 3) {
            printf("^C\r\n");
            linebuf_set(&lb, "");
            break;
        }

        if (c == 4) {
            if (lb.len == 0) {
                printf("\r\n");
                eof = 1;
                break;
            }
            continue;
        }

        if (c == 127 || c == 8) {
            linebuf_delete_before_cursor(&lb);
            redraw(prompt, &lb);
            continue;
        }

        if (c == 27) {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) {
                continue;
            }
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) {
                continue;
            }

            if (seq[0] == '[') {
                if (seq[1] == 'A') {
                    if (history_pos == -1 && history_count() > 0) {
                        strncpy(saved_line, lb.buf, sizeof(saved_line) - 1);
                    }
                    long next = history_pos + 1;
                    if ((size_t)next < history_count()) {
                        history_pos = next;
                        linebuf_set(&lb, history_get((size_t)history_pos));
                    }
                } else if (seq[1] == 'B') {
                    if (history_pos > 0) {
                        history_pos--;
                        linebuf_set(&lb, history_get((size_t)history_pos));
                    } else if (history_pos == 0) {
                        history_pos = -1;
                        linebuf_set(&lb, saved_line);
                    }
                } else if (seq[1] == 'C') {
                    if (lb.cursor < lb.len) {
                        lb.cursor++;
                    }
                } else if (seq[1] == 'D') {
                    if (lb.cursor > 0) {
                        lb.cursor--;
                    }
                } else if (seq[1] == 'H') {
                    lb.cursor = 0;
                } else if (seq[1] == 'F') {
                    lb.cursor = lb.len;
                } else if (seq[1] == '3') {
                    char tilde;
                    if (read(STDIN_FILENO, &tilde, 1) > 0) {
                        linebuf_delete_at_cursor(&lb);
                    }
                }
                redraw(prompt, &lb);
            }
            continue;
        }

        if ((unsigned char)c < 32) {
            continue;
        }

        linebuf_insert(&lb, c);
        redraw(prompt, &lb);
    }

    platform_raw_mode_disable();

    if (eof) {
        free(lb.buf);
        return NULL;
    }

    char *result = strdup(lb.buf);
    free(lb.buf);
    return result;
}
