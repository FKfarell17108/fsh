#include "core/lexer.h"

#include "env/variable.h"
#include "util/strbuf.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void tokenlist_push(TokenList *list, TokenType type, char *value) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 8;
        list->items = realloc(list->items, list->capacity * sizeof(Token));
    }
    list->items[list->count].type = type;
    list->items[list->count].value = value;
    list->count++;
}

static void append_variable(StrBuf *out, const char *input, size_t *i) {
    size_t start = *i + 1;
    size_t j = start;

    if (input[j] == '?') {
        j++;
    } else if (isalpha((unsigned char)input[j]) || input[j] == '_') {
        j++;
        while (isalnum((unsigned char)input[j]) || input[j] == '_') {
            j++;
        }
    } else {
        strbuf_push_char(out, '$');
        *i = *i + 1;
        return;
    }

    size_t len = j - start;
    char *name = malloc(len + 1);
    memcpy(name, input + start, len);
    name[len] = '\0';

    const char *value = variable_lookup(name);
    if (value) {
        strbuf_push_str(out, value);
    }

    free(name);
    *i = j;
}

static char *lex_word(const char *input, size_t *i) {
    StrBuf sb;
    strbuf_init(&sb);

    while (input[*i] != '\0') {
        char c = input[*i];

        if (c == ' ' || c == '\t') {
            break;
        }
        if (c == '|' || c == '&' || c == ';' || c == '>' || c == '<') {
            break;
        }

        if (c == '\\') {
            *i = *i + 1;
            if (input[*i] != '\0') {
                strbuf_push_char(&sb, input[*i]);
                *i = *i + 1;
            }
            continue;
        }

        if (c == '\'') {
            *i = *i + 1;
            while (input[*i] != '\0' && input[*i] != '\'') {
                strbuf_push_char(&sb, input[*i]);
                *i = *i + 1;
            }
            if (input[*i] == '\'') {
                *i = *i + 1;
            }
            continue;
        }

        if (c == '"') {
            *i = *i + 1;
            while (input[*i] != '\0' && input[*i] != '"') {
                if (input[*i] == '\\' && input[*i + 1] != '\0') {
                    *i = *i + 1;
                    strbuf_push_char(&sb, input[*i]);
                    *i = *i + 1;
                } else if (input[*i] == '$') {
                    append_variable(&sb, input, i);
                } else {
                    strbuf_push_char(&sb, input[*i]);
                    *i = *i + 1;
                }
            }
            if (input[*i] == '"') {
                *i = *i + 1;
            }
            continue;
        }

        if (c == '$') {
            append_variable(&sb, input, i);
            continue;
        }

        strbuf_push_char(&sb, c);
        *i = *i + 1;
    }

    return strbuf_take(&sb);
}

TokenList lexer_tokenize(const char *input) {
    TokenList list = {0};
    size_t i = 0;

    while (input[i] != '\0') {
        char c = input[i];

        if (c == ' ' || c == '\t') {
            i++;
            continue;
        }

        if (c == '&' && input[i + 1] == '&') {
            tokenlist_push(&list, TOKEN_AND, NULL);
            i += 2;
            continue;
        }
        if (c == '|' && input[i + 1] == '|') {
            tokenlist_push(&list, TOKEN_OR, NULL);
            i += 2;
            continue;
        }
        if (c == '>' && input[i + 1] == '>') {
            tokenlist_push(&list, TOKEN_REDIRECT_APPEND, NULL);
            i += 2;
            continue;
        }
        if (c == '>') {
            tokenlist_push(&list, TOKEN_REDIRECT_OUT, NULL);
            i++;
            continue;
        }
        if (c == '<') {
            tokenlist_push(&list, TOKEN_REDIRECT_IN, NULL);
            i++;
            continue;
        }
        if (c == '|') {
            tokenlist_push(&list, TOKEN_PIPE, NULL);
            i++;
            continue;
        }
        if (c == ';') {
            tokenlist_push(&list, TOKEN_SEMI, NULL);
            i++;
            continue;
        }
        if (c == '&') {
            tokenlist_push(&list, TOKEN_AMP, NULL);
            i++;
            continue;
        }

        size_t before = i;
        char *word = lex_word(input, &i);
        if (i == before) {
            free(word);
            i++;
            continue;
        }
        tokenlist_push(&list, TOKEN_WORD, word);
    }

    return list;
}

void lexer_free(TokenList *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].value);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}
