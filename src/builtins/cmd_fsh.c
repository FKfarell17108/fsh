#include "builtins/builtin.h"

#include <stdio.h>

#define FSH_VERSION "0.1.0"

int cmd_fsh(int argc, char **args, int *exit_code) {
    (void)argc;
    (void)args;
    printf("\nFSH (FK Shell) - C Edition\n\n");
    printf(" Developer  Farell Kurniawan\n");
    printf(" Version    v%s\n\n", FSH_VERSION);
    printf("Type 'fshrc' for config.\n\n");
    *exit_code = 0;
    return 1;
}
