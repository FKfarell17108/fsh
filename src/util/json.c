#include "util/json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *s;
    size_t pos;
    size_t len;
} JsonParser;

static void skip_ws(JsonParser *p) {
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else {
            break;
        }
    }
}

static int peek(JsonParser *p) {
    if (p->pos >= p->len) {
        return -1;
    }
    return (unsigned char)p->s[p->pos];
}

static JsonValue *json_alloc(JsonType type) {
    JsonValue *v = malloc(sizeof(JsonValue));
    v->type = type;
    memset(&v->as, 0, sizeof(v->as));
    return v;
}

static void utf8_append_codepoint(StrBuf *sb, unsigned int cp) {
    if (cp <= 0x7F) {
        strbuf_push_char(sb, (char)cp);
    } else if (cp <= 0x7FF) {
        strbuf_push_char(sb, (char)(0xC0 | (cp >> 6)));
        strbuf_push_char(sb, (char)(0x80 | (cp & 0x3F)));
    } else {
        strbuf_push_char(sb, (char)(0xE0 | (cp >> 12)));
        strbuf_push_char(sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
        strbuf_push_char(sb, (char)(0x80 | (cp & 0x3F)));
    }
}

static char *parse_raw_string(JsonParser *p) {
    if (peek(p) != '"') {
        return NULL;
    }
    p->pos++;

    StrBuf sb;
    strbuf_init(&sb);

    while (p->pos < p->len && p->s[p->pos] != '"') {
        char c = p->s[p->pos];
        if (c == '\\' && p->pos + 1 < p->len) {
            char esc = p->s[p->pos + 1];
            switch (esc) {
                case '"': strbuf_push_char(&sb, '"'); p->pos += 2; break;
                case '\\': strbuf_push_char(&sb, '\\'); p->pos += 2; break;
                case '/': strbuf_push_char(&sb, '/'); p->pos += 2; break;
                case 'b': strbuf_push_char(&sb, '\b'); p->pos += 2; break;
                case 'f': strbuf_push_char(&sb, '\f'); p->pos += 2; break;
                case 'n': strbuf_push_char(&sb, '\n'); p->pos += 2; break;
                case 'r': strbuf_push_char(&sb, '\r'); p->pos += 2; break;
                case 't': strbuf_push_char(&sb, '\t'); p->pos += 2; break;
                case 'u': {
                    if (p->pos + 6 <= p->len) {
                        char hex[5];
                        memcpy(hex, p->s + p->pos + 2, 4);
                        hex[4] = '\0';
                        unsigned int cp = (unsigned int)strtoul(hex, NULL, 16);
                        utf8_append_codepoint(&sb, cp);
                        p->pos += 6;
                    } else {
                        p->pos += 2;
                    }
                    break;
                }
                default:
                    strbuf_push_char(&sb, esc);
                    p->pos += 2;
                    break;
            }
        } else {
            strbuf_push_char(&sb, c);
            p->pos++;
        }
    }

    if (p->pos < p->len) {
        p->pos++;
    }

    return strbuf_take(&sb);
}

static JsonValue *parse_value(JsonParser *p);

static JsonValue *parse_string_value(JsonParser *p) {
    char *s = parse_raw_string(p);
    if (!s) {
        return NULL;
    }
    JsonValue *v = json_alloc(JSON_STRING);
    v->as.string = s;
    return v;
}

static JsonValue *parse_number_value(JsonParser *p) {
    size_t start = p->pos;
    if (peek(p) == '-') {
        p->pos++;
    }
    while (p->pos < p->len && isdigit((unsigned char)p->s[p->pos])) {
        p->pos++;
    }
    if (p->pos < p->len && p->s[p->pos] == '.') {
        p->pos++;
        while (p->pos < p->len && isdigit((unsigned char)p->s[p->pos])) {
            p->pos++;
        }
    }
    if (p->pos < p->len && (p->s[p->pos] == 'e' || p->s[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->len && (p->s[p->pos] == '+' || p->s[p->pos] == '-')) {
            p->pos++;
        }
        while (p->pos < p->len && isdigit((unsigned char)p->s[p->pos])) {
            p->pos++;
        }
    }

    size_t len = p->pos - start;
    char buf[64];
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, p->s + start, len);
    buf[len] = '\0';

    JsonValue *v = json_alloc(JSON_NUMBER);
    v->as.number = strtod(buf, NULL);
    return v;
}

static JsonValue *parse_array_value(JsonParser *p) {
    p->pos++;
    JsonValue *v = json_alloc(JSON_ARRAY);
    size_t capacity = 0;

    skip_ws(p);
    if (peek(p) == ']') {
        p->pos++;
        return v;
    }

    for (;;) {
        skip_ws(p);
        JsonValue *item = parse_value(p);
        if (!item) {
            break;
        }
        if (v->as.array.count == capacity) {
            capacity = capacity ? capacity * 2 : 8;
            v->as.array.items = realloc(v->as.array.items, capacity * sizeof(JsonValue *));
        }
        v->as.array.items[v->as.array.count++] = item;

        skip_ws(p);
        if (peek(p) == ',') {
            p->pos++;
            continue;
        }
        break;
    }

    skip_ws(p);
    if (peek(p) == ']') {
        p->pos++;
    }

    return v;
}

static JsonValue *parse_object_value(JsonParser *p) {
    p->pos++;
    JsonValue *v = json_alloc(JSON_OBJECT);
    size_t capacity = 0;

    skip_ws(p);
    if (peek(p) == '}') {
        p->pos++;
        return v;
    }

    for (;;) {
        skip_ws(p);
        if (peek(p) != '"') {
            break;
        }
        char *key = parse_raw_string(p);
        skip_ws(p);
        if (peek(p) != ':') {
            free(key);
            break;
        }
        p->pos++;
        skip_ws(p);
        JsonValue *value = parse_value(p);
        if (!value) {
            free(key);
            break;
        }

        if (v->as.object.count == capacity) {
            capacity = capacity ? capacity * 2 : 8;
            v->as.object.members = realloc(v->as.object.members, capacity * sizeof(JsonMember));
        }
        v->as.object.members[v->as.object.count].key = key;
        v->as.object.members[v->as.object.count].value = value;
        v->as.object.count++;

        skip_ws(p);
        if (peek(p) == ',') {
            p->pos++;
            continue;
        }
        break;
    }

    skip_ws(p);
    if (peek(p) == '}') {
        p->pos++;
    }

    return v;
}

static JsonValue *parse_value(JsonParser *p) {
    skip_ws(p);
    int c = peek(p);

    if (c == '"') {
        return parse_string_value(p);
    }
    if (c == '{') {
        return parse_object_value(p);
    }
    if (c == '[') {
        return parse_array_value(p);
    }
    if (c == '-' || isdigit(c)) {
        return parse_number_value(p);
    }
    if (strncmp(p->s + p->pos, "true", 4) == 0) {
        p->pos += 4;
        JsonValue *v = json_alloc(JSON_BOOL);
        v->as.boolean = 1;
        return v;
    }
    if (strncmp(p->s + p->pos, "false", 5) == 0) {
        p->pos += 5;
        JsonValue *v = json_alloc(JSON_BOOL);
        v->as.boolean = 0;
        return v;
    }
    if (strncmp(p->s + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return json_alloc(JSON_NULL);
    }

    return NULL;
}

JsonValue *json_parse(const char *text) {
    JsonParser p = {text, 0, strlen(text)};
    return parse_value(&p);
}

JsonValue *json_parse_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)size + 1);
    size_t read = fread(buf, 1, (size_t)size, f);
    buf[read] = '\0';
    fclose(f);

    JsonValue *v = json_parse(buf);
    free(buf);
    return v;
}

void json_free(JsonValue *value) {
    if (!value) {
        return;
    }
    switch (value->type) {
        case JSON_STRING:
            free(value->as.string);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < value->as.array.count; i++) {
                json_free(value->as.array.items[i]);
            }
            free(value->as.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < value->as.object.count; i++) {
                free(value->as.object.members[i].key);
                json_free(value->as.object.members[i].value);
            }
            free(value->as.object.members);
            break;
        default:
            break;
    }
    free(value);
}

const JsonValue *json_object_get(const JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT) {
        return NULL;
    }
    for (size_t i = 0; i < obj->as.object.count; i++) {
        if (strcmp(obj->as.object.members[i].key, key) == 0) {
            return obj->as.object.members[i].value;
        }
    }
    return NULL;
}

size_t json_array_count(const JsonValue *arr) {
    if (!arr || arr->type != JSON_ARRAY) {
        return 0;
    }
    return arr->as.array.count;
}

const JsonValue *json_array_get(const JsonValue *arr, size_t index) {
    if (!arr || arr->type != JSON_ARRAY || index >= arr->as.array.count) {
        return NULL;
    }
    return arr->as.array.items[index];
}

const char *json_as_string(const JsonValue *v, const char *fallback) {
    if (!v || v->type != JSON_STRING) {
        return fallback;
    }
    return v->as.string;
}

double json_as_number(const JsonValue *v, double fallback) {
    if (!v || v->type != JSON_NUMBER) {
        return fallback;
    }
    return v->as.number;
}

int json_as_bool(const JsonValue *v, int fallback) {
    if (!v || v->type != JSON_BOOL) {
        return fallback;
    }
    return v->as.boolean;
}

void json_write_escaped_string(StrBuf *sb, const char *s) {
    strbuf_push_char(sb, '"');
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '"': strbuf_push_str(sb, "\\\""); break;
            case '\\': strbuf_push_str(sb, "\\\\"); break;
            case '\b': strbuf_push_str(sb, "\\b"); break;
            case '\f': strbuf_push_str(sb, "\\f"); break;
            case '\n': strbuf_push_str(sb, "\\n"); break;
            case '\r': strbuf_push_str(sb, "\\r"); break;
            case '\t': strbuf_push_str(sb, "\\t"); break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    strbuf_push_str(sb, buf);
                } else {
                    strbuf_push_char(sb, (char)c);
                }
                break;
        }
    }
    strbuf_push_char(sb, '"');
}
