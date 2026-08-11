#include "builtins/builtin.h"

#include "env/alias.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_alias(int argc, char **args, int *exit_code) {
    if (argc == 0) {
        size_t count;
        const AliasEntry *list = alias_list(&count);
        if (count == 0) {
            printf("(no aliases defined)\n");
        } else {
            for (size_t i = 0; i < count; i++) {
                printf("alias %s='%s'\n", list[i].name, list[i].value);
            }
        }
        *exit_code = 0;
        return 1;
    }

    for (int i = 0; i < argc; i++) {
        char *arg = args[i];
        char *eq = strchr(arg, '=');
        if (!eq) {
            const char *val = alias_get(arg);
            if (val) {
                printf("alias %s='%s'\n", arg, val);
            } else {
                printf("fsh: alias: %s: not found\n", arg);
            }
            continue;
        }

        size_t name_len = (size_t)(eq - arg);
        char *name = malloc(name_len + 1);
        memcpy(name, arg, name_len);
        name[name_len] = '\0';

        char *value = strdup(eq + 1);
        size_t vlen = strlen(value);
        if (vlen >= 2) {
            if ((value[0] == '\'' && value[vlen - 1] == '\'') ||
                (value[0] == '"' && value[vlen - 1] == '"')) {
                value[vlen - 1] = '\0';
                memmove(value, value + 1, vlen - 1);
            }
        }

        if (name_len == 0) {
            printf("fsh: alias: invalid name\n");
        } else {
            alias_set(name, value);
        }

        free(name);
        free(value);
    }

    *exit_code = 0;
    return 1;
}

int cmd_unalias(int argc, char **args, int *exit_code) {
    if (argc == 0) {
        printf("usage: unalias <name>\n");
        *exit_code = 0;
        return 1;
    }

    for (int i = 0; i < argc; i++) {
        if (!alias_remove(args[i])) {
            printf("fsh: unalias: %s: not found\n", args[i]);
        }
    }

    *exit_code = 0;
    return 1;
}
