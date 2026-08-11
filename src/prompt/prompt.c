#include "prompt/prompt.h"

#include "env/shell_state.h"

#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *prompt_build(void) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        strcpy(cwd, "?");
    }

    char cwd_copy[PATH_MAX];
    strncpy(cwd_copy, cwd, sizeof(cwd_copy) - 1);
    cwd_copy[sizeof(cwd_copy) - 1] = '\0';
    char *folder = basename(cwd_copy);

    int code = shell_state_last_exit_code();
    char *result = malloc(512);

    if (code != 0) {
        snprintf(result, 512, "fsh/\x1b[34m%s\x1b[0m\x1b[31m > \x1b[0m", folder);
    } else {
        snprintf(result, 512, "fsh/\x1b[34m%s\x1b[0m > ", folder);
    }

    return result;
}
