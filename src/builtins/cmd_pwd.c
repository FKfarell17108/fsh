#include "builtins/builtin.h"

#include <limits.h>
#include <stdio.h>
#include <unistd.h>

int cmd_pwd(int argc, char **args, int *exit_code) {
    (void)argc;
    (void)args;

    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf))) {
        printf("%s\n", buf);
        *exit_code = 0;
    } else {
        *exit_code = 1;
    }
    return 1;
}
