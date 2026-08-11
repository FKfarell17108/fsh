#include "builtins/builtin.h"

#include <stdlib.h>

int cmd_exit(int argc, char **args, int *exit_code) {
    int code = 0;
    if (argc > 0) {
        code = atoi(args[0]);
    }
    (void)exit_code;
    exit(code);
}
