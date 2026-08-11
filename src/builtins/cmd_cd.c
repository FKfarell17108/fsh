#include "builtins/builtin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int cmd_cd(int argc, char **args, int *exit_code) {
    const char *home = getenv("HOME");
    char target[4096];

    if (argc == 0 || strcmp(args[0], "~") == 0) {
        snprintf(target, sizeof(target), "%s", home ? home : "");
    } else if (strncmp(args[0], "~/", 2) == 0 && home) {
        snprintf(target, sizeof(target), "%s/%s", home, args[0] + 2);
    } else {
        snprintf(target, sizeof(target), "%s", args[0]);
    }

    if (chdir(target) != 0) {
        fprintf(stderr, "cd: %s: No such file or directory\n", target);
        *exit_code = 1;
        return 1;
    }

    *exit_code = 0;
    return 1;
}
