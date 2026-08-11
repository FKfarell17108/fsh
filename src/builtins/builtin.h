#ifndef FSH_BUILTINS_BUILTIN_H
#define FSH_BUILTINS_BUILTIN_H

#include "core/ast.h"

int builtin_handle(const Command *cmd, int *exit_code);
int builtin_is_name(const char *name);

#endif
