#ifndef FSH_UTIL_JSON_H
#define FSH_UTIL_JSON_H

#include "util/strbuf.h"

#include <stddef.h>

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

typedef struct {
    char *key;
    JsonValue *value;
} JsonMember;

struct JsonValue {
    JsonType type;
    union {
        int boolean;
        double number;
        char *string;
        struct {
            JsonValue **items;
            size_t count;
        } array;
        struct {
            JsonMember *members;
            size_t count;
        } object;
    } as;
};

JsonValue *json_parse(const char *text);
JsonValue *json_parse_file(const char *path);
void json_free(JsonValue *value);

const JsonValue *json_object_get(const JsonValue *obj, const char *key);
size_t json_array_count(const JsonValue *arr);
const JsonValue *json_array_get(const JsonValue *arr, size_t index);

const char *json_as_string(const JsonValue *v, const char *fallback);
double json_as_number(const JsonValue *v, double fallback);
int json_as_bool(const JsonValue *v, int fallback);

void json_write_escaped_string(StrBuf *sb, const char *s);

#endif
