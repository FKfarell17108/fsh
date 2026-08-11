#include "builtins/builtin.h"

#include <string.h>

int cmd_cd(int argc, char **args, int *exit_code);
int cmd_pwd(int argc, char **args, int *exit_code);
int cmd_exit(int argc, char **args, int *exit_code);
int cmd_echo(int argc, char **args, int *exit_code);
int cmd_type(int argc, char **args, int *exit_code);
int cmd_alias(int argc, char **args, int *exit_code);
int cmd_unalias(int argc, char **args, int *exit_code);
int cmd_source(int argc, char **args, int *exit_code);
int cmd_clear(int argc, char **args, int *exit_code);
int cmd_fshrc(int argc, char **args, int *exit_code);
int cmd_fsh(int argc, char **args, int *exit_code);

static const char *BUILTIN_NAMES[] = {
    "cd", "pwd", "exit", "echo", "type", "alias", "unalias",
    "source", "clear", "cls", "fshrc", "fsh", NULL
};

int builtin_is_name(const char *name) {
    for (int i = 0; BUILTIN_NAMES[i] != NULL; i++) {
        if (strcmp(BUILTIN_NAMES[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

int builtin_handle(const Command *cmd, int *exit_code) {
    const char *name = cmd->cmd;
    char **args = cmd->args;
    int argc = (int)cmd->argc;

    if (strcmp(name, "cd") == 0) return cmd_cd(argc, args, exit_code);
    if (strcmp(name, "pwd") == 0) return cmd_pwd(argc, args, exit_code);
    if (strcmp(name, "exit") == 0) return cmd_exit(argc, args, exit_code);
    if (strcmp(name, "echo") == 0) return cmd_echo(argc, args, exit_code);
    if (strcmp(name, "type") == 0) return cmd_type(argc, args, exit_code);
    if (strcmp(name, "alias") == 0) return cmd_alias(argc, args, exit_code);
    if (strcmp(name, "unalias") == 0) return cmd_unalias(argc, args, exit_code);
    if (strcmp(name, "source") == 0) return cmd_source(argc, args, exit_code);
    if (strcmp(name, "clear") == 0 || strcmp(name, "cls") == 0) return cmd_clear(argc, args, exit_code);
    if (strcmp(name, "fshrc") == 0) return cmd_fshrc(argc, args, exit_code);
    if (strcmp(name, "fsh") == 0) return cmd_fsh(argc, args, exit_code);

    return 0;
}
