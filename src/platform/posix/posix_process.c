#include "platform/platform.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int platform_spawn(const Command *cmd, PlatformIO io, const int *close_fds, size_t close_fd_count,
                    pid_t *out_pid) {
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        for (size_t k = 0; k < close_fd_count; k++) {
            if (close_fds[k] >= 0) {
                close(close_fds[k]);
            }
        }

        if (io.stdin_fd != -1) {
            dup2(io.stdin_fd, STDIN_FILENO);
            if (io.stdin_fd != STDIN_FILENO) {
                close(io.stdin_fd);
            }
        }
        if (io.stdout_fd != -1) {
            dup2(io.stdout_fd, STDOUT_FILENO);
            if (io.stdout_fd != STDOUT_FILENO) {
                close(io.stdout_fd);
            }
        }
        if (io.stderr_fd != -1) {
            dup2(io.stderr_fd, STDERR_FILENO);
            if (io.stderr_fd != STDERR_FILENO) {
                close(io.stderr_fd);
            }
        }

        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);

        char **argv = malloc((cmd->argc + 2) * sizeof(char *));
        argv[0] = cmd->cmd;
        for (size_t i = 0; i < cmd->argc; i++) {
            argv[i + 1] = cmd->args[i];
        }
        argv[cmd->argc + 1] = NULL;

        execvp(cmd->cmd, argv);

        if (errno == ENOENT) {
            fprintf(stderr, "fsh: %s: command not found\n", cmd->cmd);
            _exit(127);
        } else if (errno == EACCES) {
            fprintf(stderr, "fsh: %s: permission denied\n", cmd->cmd);
            _exit(126);
        } else {
            fprintf(stderr, "fsh: %s: %s\n", cmd->cmd, strerror(errno));
            _exit(126);
        }
    }

    *out_pid = pid;
    return 0;
}

int platform_wait_pid(pid_t pid, int *exit_code) {
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }

    if (WIFEXITED(status)) {
        *exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        *exit_code = 128 + WTERMSIG(status);
    } else {
        *exit_code = 1;
    }
    return 0;
}

void platform_reap_background(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        (void)status;
    }
}
