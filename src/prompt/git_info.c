#include "prompt/git_info.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int find_git_root(char *out, size_t out_size) {
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        return 0;
    }

    char path[4096];
    snprintf(path, sizeof(path), "%s", cwd);

    for (;;) {
        char git_marker[4160];
        snprintf(git_marker, sizeof(git_marker), "%s/.git", path);

        struct stat st;
        if (stat(git_marker, &st) == 0) {
            snprintf(out, out_size, "%s", path);
            return 1;
        }

        char *slash = strrchr(path, '/');
        if (!slash || slash == path) {
            break;
        }
        *slash = '\0';
        if (path[0] == '\0') {
            snprintf(path, sizeof(path), "/");
        }
    }

    return 0;
}

static int run_capture(char *const argv[], char *out, size_t out_size) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);

    size_t total = 0;
    ssize_t n;
    while (total < out_size - 1 && (n = read(pipefd[0], out + total, out_size - 1 - total)) > 0) {
        total += (size_t)n;
    }
    out[total] = '\0';
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);
    return 0;
}

GitInfo git_info_collect(void) {
    GitInfo info;
    memset(&info, 0, sizeof(info));

    char root[4096];
    if (!find_git_root(root, sizeof(root))) {
        return info;
    }

    char *argv[] = {"git", "-C", root, "status", "--porcelain=v2", "--branch", NULL};
    char output[65536];
    if (run_capture(argv, output, sizeof(output)) != 0) {
        return info;
    }

    info.in_repo = 1;
    snprintf(info.branch, sizeof(info.branch), "?");

    char *saveptr = NULL;
    char *line = strtok_r(output, "\n", &saveptr);
    while (line) {
        if (strncmp(line, "# branch.head ", 14) == 0) {
            snprintf(info.branch, sizeof(info.branch), "%s", line + 14);
        } else if (strncmp(line, "# branch.ab ", 12) == 0) {
            int ahead = 0;
            int behind = 0;
            sscanf(line + 12, "+%d -%d", &ahead, &behind);
            info.ahead = ahead;
            info.behind = behind;
        } else if (line[0] == '1' || line[0] == '2') {
            if (line[2] != '.') {
                info.staged = 1;
            }
            if (line[3] != '.') {
                info.dirty = 1;
            }
        } else if (line[0] == 'u') {
            info.dirty = 1;
        } else if (line[0] == '?') {
            info.untracked = 1;
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    return info;
}
