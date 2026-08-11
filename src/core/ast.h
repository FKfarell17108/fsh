#ifndef FSH_CORE_AST_H
#define FSH_CORE_AST_H

#include <stddef.h>

typedef enum {
    REDIRECT_OUT,
    REDIRECT_APPEND,
    REDIRECT_IN
} RedirectType;

typedef struct {
    RedirectType type;
    char *file;
} Redirect;

typedef struct {
    char *cmd;
    char **args;
    size_t argc;
    Redirect *redirects;
    size_t redirect_count;
} Command;

typedef struct {
    Command *commands;
    size_t count;
    int background;
} Pipeline;

typedef enum {
    STATEMENT_PIPELINE,
    STATEMENT_AND,
    STATEMENT_OR,
    STATEMENT_SEQ
} StatementKind;

typedef struct Statement {
    StatementKind kind;
    Pipeline pipeline;
    struct Statement *left;
    struct Statement *right;
} Statement;

Statement *statement_new_pipeline(Pipeline pipeline);
Statement *statement_new_binary(StatementKind kind, Statement *left, Statement *right);
void statement_free(Statement *stmt);
void command_free(Command *cmd);
void pipeline_free(Pipeline *pipeline);

#endif
