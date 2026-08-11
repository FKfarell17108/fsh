#include "prompt/prompt.h"

#include "env/shell_state.h"
#include "prompt/git_info.h"

#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void append_git_segment(char *buf, size_t buf_size) {
    GitInfo git = git_info_collect();
    if (!git.in_repo) {
        buf[0] = '\0';
        return;
    }

    char indicators[128] = {0};
    if (git.dirty) {
        strcat(indicators, " \x1b[33m\u25cf\x1b[0m");
    }
    if (git.staged) {
        strcat(indicators, " \x1b[32m\u271a\x1b[0m");
    }
    if (git.untracked) {
        strcat(indicators, " \x1b[31m\u2026\x1b[0m");
    }
    if (git.ahead > 0) {
        char piece[32];
        snprintf(piece, sizeof(piece), " \x1b[36m\u2191%d\x1b[0m", git.ahead);
        strcat(indicators, piece);
    }
    if (git.behind > 0) {
        char piece[32];
        snprintf(piece, sizeof(piece), " \x1b[36m\u2193%d\x1b[0m", git.behind);
        strcat(indicators, piece);
    }

    snprintf(buf, buf_size, " \x1b[35m(%s%s\x1b[35m)\x1b[0m", git.branch, indicators);
}

char *prompt_build(void) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        strcpy(cwd, "?");
    }

    char cwd_copy[PATH_MAX];
    strncpy(cwd_copy, cwd, sizeof(cwd_copy) - 1);
    cwd_copy[sizeof(cwd_copy) - 1] = '\0';
    char *folder = basename(cwd_copy);

    char git_segment[512];
    append_git_segment(git_segment, sizeof(git_segment));

    int code = shell_state_last_exit_code();
    char *result = malloc(1024);

    if (code != 0) {
        snprintf(result, 1024, "fsh/\x1b[34m%s\x1b[0m%s\x1b[31m > \x1b[0m", folder, git_segment);
    } else {
        snprintf(result, 1024, "fsh/\x1b[34m%s\x1b[0m%s > ", folder, git_segment);
    }

    return result;
}
