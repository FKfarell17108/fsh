#include "builtins/builtin.h"

#include <stdio.h>

int cmd_clear(int argc, char **args, int *exit_code) {
    (void)argc;
    (void)args;
    printf("\x1b[3J\x1b[2J\x1b[H");
    fflush(stdout);
    *exit_code = 0;
    return 1;
}
