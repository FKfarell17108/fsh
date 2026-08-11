#include "builtins/builtin.h"

#include "env/alias.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int cmd_type(int argc, char **args, int *exit_code) {
    if (argc == 0) {
        *exit_code = 0;
        return 1;
    }
    const char *target = args[0];

    const char *alias_val = alias_get(target);
    if (alias_val) {
        printf("%s is aliased to '%s'\n", target, alias_val);
        *exit_code = 0;
        return 1;
    }

    if (builtin_is_name(target)) {
        printf("%s is a shell builtin\n", target);
        *exit_code = 0;
        return 1;
    }

    const char *path_env = getenv("PATH");
    if (path_env) {
        char *copy = strdup(path_env);
        char *saveptr = NULL;
        char *dir = strtok_r(copy, ":", &saveptr);
        while (dir) {
            char full[4096];
            snprintf(full, sizeof(full), "%s/%s", dir, target);
            if (access(full, F_OK) == 0) {
                printf("%s is %s\n", target, full);
                free(copy);
                *exit_code = 0;
                return 1;
            }
            dir = strtok_r(NULL, ":", &saveptr);
        }
        free(copy);
    }

    printf("%s: not found\n", target);
    *exit_code = 1;
    return 1;
}
