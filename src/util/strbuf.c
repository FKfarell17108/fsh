#include "util/strbuf.h"

#include <stdlib.h>
#include <string.h>

void strbuf_init(StrBuf *sb) {
    sb->data = malloc(1);
    sb->data[0] = '\0';
    sb->length = 0;
    sb->capacity = 1;
}

static void strbuf_ensure(StrBuf *sb, size_t extra) {
    if (sb->length + extra + 1 <= sb->capacity) {
        return;
    }
    size_t new_cap = sb->capacity ? sb->capacity : 1;
    while (new_cap < sb->length + extra + 1) {
        new_cap *= 2;
    }
    sb->data = realloc(sb->data, new_cap);
    sb->capacity = new_cap;
}

void strbuf_push_char(StrBuf *sb, char c) {
    strbuf_ensure(sb, 1);
    sb->data[sb->length++] = c;
    sb->data[sb->length] = '\0';
}

void strbuf_push_str(StrBuf *sb, const char *s) {
    size_t len = strlen(s);
    strbuf_ensure(sb, len);
    memcpy(sb->data + sb->length, s, len);
    sb->length += len;
    sb->data[sb->length] = '\0';
}

char *strbuf_take(StrBuf *sb) {
    char *result = sb->data;
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
    return result;
}

void strbuf_free(StrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
}
