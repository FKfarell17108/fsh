#include "core/parser.h"

#include "core/lexer.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    Token *tokens;
    size_t count;
    size_t pos;
} ParserState;

static Token *peek(ParserState *p) {
    if (p->pos >= p->count) {
        return NULL;
    }
    return &p->tokens[p->pos];
}

static Token *advance(ParserState *p) {
    if (p->pos >= p->count) {
        return NULL;
    }
    return &p->tokens[p->pos++];
}

static void redirects_push(Redirect **arr, size_t *count, RedirectType type, char *file) {
    *arr = realloc(*arr, (*count + 1) * sizeof(Redirect));
    (*arr)[*count].type = type;
    (*arr)[*count].file = file;
    (*count)++;
}

static void args_push(char ***arr, size_t *count, char *value) {
    *arr = realloc(*arr, (*count + 1) * sizeof(char *));
    (*arr)[*count] = value;
    (*count)++;
}

static void commands_push(Command **arr, size_t *count, Command cmd) {
    *arr = realloc(*arr, (*count + 1) * sizeof(Command));
    (*arr)[*count] = cmd;
    (*count)++;
}

static int parse_command(ParserState *p, Command *out) {
    char **words = NULL;
    size_t word_count = 0;
    Redirect *redirects = NULL;
    size_t redirect_count = 0;

    for (;;) {
        Token *t = peek(p);
        if (!t) {
            break;
        }

        if (t->type == TOKEN_WORD) {
            advance(p);
            args_push(&words, &word_count, strdup(t->value));
            continue;
        }

        if (t->type == TOKEN_REDIRECT_OUT || t->type == TOKEN_REDIRECT_APPEND ||
            t->type == TOKEN_REDIRECT_IN) {
            advance(p);
            Token *file_token = peek(p);
            if (file_token && file_token->type == TOKEN_WORD) {
                advance(p);
                RedirectType type = t->type == TOKEN_REDIRECT_OUT   ? REDIRECT_OUT
                                     : t->type == TOKEN_REDIRECT_APPEND ? REDIRECT_APPEND
                                                                         : REDIRECT_IN;
                redirects_push(&redirects, &redirect_count, type, strdup(file_token->value));
            }
            continue;
        }

        break;
    }

    if (word_count == 0) {
        free(words);
        free(redirects);
        return 0;
    }

    out->cmd = strdup(words[0]);
    out->argc = word_count - 1;
    if (out->argc > 0) {
        out->args = malloc(out->argc * sizeof(char *));
        for (size_t i = 0; i < out->argc; i++) {
            out->args[i] = words[i + 1];
        }
    } else {
        out->args = NULL;
    }
    free(words[0]);
    free(words);
    out->redirects = redirects;
    out->redirect_count = redirect_count;
    return 1;
}

static Statement *parse_pipeline(ParserState *p) {
    Command first;
    if (!parse_command(p, &first)) {
        return NULL;
    }

    Command *commands = NULL;
    size_t count = 0;
    commands_push(&commands, &count, first);

    while (peek(p) && peek(p)->type == TOKEN_PIPE) {
        advance(p);
        Command next;
        if (parse_command(p, &next)) {
            commands_push(&commands, &count, next);
        } else {
            break;
        }
    }

    int background = 0;
    if (peek(p) && peek(p)->type == TOKEN_AMP) {
        advance(p);
        background = 1;
    }

    Pipeline pipeline;
    pipeline.commands = commands;
    pipeline.count = count;
    pipeline.background = background;

    return statement_new_pipeline(pipeline);
}

static Statement *parse_and_or(ParserState *p) {
    Statement *left = parse_pipeline(p);
    if (!left) {
        return NULL;
    }

    for (;;) {
        Token *t = peek(p);
        if (t && t->type == TOKEN_AND) {
            advance(p);
            Statement *right = parse_pipeline(p);
            if (!right) {
                break;
            }
            left = statement_new_binary(STATEMENT_AND, left, right);
        } else if (t && t->type == TOKEN_OR) {
            advance(p);
            Statement *right = parse_pipeline(p);
            if (!right) {
                break;
            }
            left = statement_new_binary(STATEMENT_OR, left, right);
        } else {
            break;
        }
    }

    return left;
}

static Statement *parse_statement(ParserState *p) {
    Statement *left = parse_and_or(p);
    if (!left) {
        return NULL;
    }

    while (peek(p) && peek(p)->type == TOKEN_SEMI) {
        advance(p);
        Statement *right = parse_and_or(p);
        if (!right) {
            break;
        }
        left = statement_new_binary(STATEMENT_SEQ, left, right);
    }

    return left;
}

Statement *parser_parse(const char *input) {
    TokenList tokens = lexer_tokenize(input);
    if (tokens.count == 0) {
        lexer_free(&tokens);
        return NULL;
    }

    ParserState state = {tokens.items, tokens.count, 0};
    Statement *result = parse_statement(&state);

    lexer_free(&tokens);
    return result;
}
