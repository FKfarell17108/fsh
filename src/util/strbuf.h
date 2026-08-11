#ifndef FSH_UTIL_STRBUF_H
#define FSH_UTIL_STRBUF_H

#include <stddef.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} StrBuf;

void strbuf_init(StrBuf *sb);
void strbuf_push_char(StrBuf *sb, char c);
void strbuf_push_str(StrBuf *sb, const char *s);
char *strbuf_take(StrBuf *sb);
void strbuf_free(StrBuf *sb);

#endif
