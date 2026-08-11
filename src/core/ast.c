#include "core/ast.h"

#include <stdlib.h>

Statement *statement_new_pipeline(Pipeline pipeline) {
    Statement *s = malloc(sizeof(Statement));
    s->kind = STATEMENT_PIPELINE;
    s->pipeline = pipeline;
    s->left = NULL;
    s->right = NULL;
    return s;
}

Statement *statement_new_binary(StatementKind kind, Statement *left, Statement *right) {
    Statement *s = malloc(sizeof(Statement));
    s->kind = kind;
    s->pipeline.commands = NULL;
    s->pipeline.count = 0;
    s->pipeline.background = 0;
    s->left = left;
    s->right = right;
    return s;
}

void command_free(Command *cmd) {
    free(cmd->cmd);
    for (size_t i = 0; i < cmd->argc; i++) {
        free(cmd->args[i]);
    }
    free(cmd->args);
    for (size_t i = 0; i < cmd->redirect_count; i++) {
        free(cmd->redirects[i].file);
    }
    free(cmd->redirects);
}

void pipeline_free(Pipeline *pipeline) {
    for (size_t i = 0; i < pipeline->count; i++) {
        command_free(&pipeline->commands[i]);
    }
    free(pipeline->commands);
}

void statement_free(Statement *stmt) {
    if (!stmt) {
        return;
    }
    if (stmt->kind == STATEMENT_PIPELINE) {
        pipeline_free(&stmt->pipeline);
    } else {
        statement_free(stmt->left);
        statement_free(stmt->right);
    }
    free(stmt);
}
