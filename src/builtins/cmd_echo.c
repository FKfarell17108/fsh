#include "builtins/builtin.h"

#include <stdio.h>

int cmd_echo(int argc, char **args, int *exit_code) {
    for (int i = 0; i < argc; i++) {
        printf("%s", args[i]);
        if (i + 1 < argc) {
            printf(" ");
        }
    }
    printf("\n");
    *exit_code = 0;
    return 1;
}
