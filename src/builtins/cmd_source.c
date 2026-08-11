#include "builtins/builtin.h"

#include "env/fshrc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_source(int argc, char **args, int *exit_code) {
    if (argc == 0) {
        printf("source: usage: source filename\n");
        *exit_code = 0;
        return 1;
    }

    const char *target = args[0];
    const char *rc_path = fshrc_path();

    char home_target[4096] = {0};
    const char *home = getenv("HOME");
    if (strncmp(target, "~/", 2) == 0 && home) {
        snprintf(home_target, sizeof(home_target), "%s/%s", home, target + 2);
        target = home_target;
    }

    if (strcmp(target, rc_path) == 0 || strcmp(args[0], ".fshrc") == 0) {
        fshrc_load();
        printf("status: fsh reloaded\n");
        *exit_code = 0;
    } else {
        printf("fsh: source: currently only sourcing ~/.fshrc is supported\n");
        *exit_code = 1;
    }

    return 1;
}
