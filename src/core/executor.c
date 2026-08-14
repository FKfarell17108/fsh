#include "core/executor.h"

#include "builtins/builtin.h"
#include "env/shell_state.h"
#include "platform/platform.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void execute_statement(Statement *stmt);

static int resolve_stdin(const Command *cmd) {
    for (size_t i = 0; i < cmd->redirect_count; i++) {
        if (cmd->redirects[i].type == REDIRECT_IN) {
            int fd = open(cmd->redirects[i].file, O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "fsh: %s: No such file or directory\n", cmd->redirects[i].file);
                return -2;
            }
            return fd;
        }
    }
    return -1;
}

static int resolve_stdout(const Command *cmd) {
    for (size_t i = 0; i < cmd->redirect_count; i++) {
        if (cmd->redirects[i].type == REDIRECT_OUT || cmd->redirects[i].type == REDIRECT_APPEND) {
            int flags = O_WRONLY | O_CREAT;
            flags |= cmd->redirects[i].type == REDIRECT_APPEND ? O_APPEND : O_TRUNC;
            int fd = open(cmd->redirects[i].file, flags, 0644);
            if (fd < 0) {
                fprintf(stderr, "fsh: %s: Permission denied\n", cmd->redirects[i].file);
                return -2;
            }
            return fd;
        }
    }
    return -1;
}

static void run_single(const Command *cmd, int background) {
    if (builtin_is_name(cmd->cmd)) {
        int in_fd = resolve_stdin(cmd);
        if (in_fd == -2) {
            shell_state_set_last_exit_code(1);
            return;
        }
        int out_fd = resolve_stdout(cmd);
        if (out_fd == -2) {
            if (in_fd >= 0) {
                close(in_fd);
            }
            shell_state_set_last_exit_code(1);
            return;
        }

        int saved_stdin = -1;
        int saved_stdout = -1;
        if (in_fd >= 0) {
            saved_stdin = dup(STDIN_FILENO);
            dup2(in_fd, STDIN_FILENO);
            close(in_fd);
        }
        if (out_fd >= 0) {
            saved_stdout = dup(STDOUT_FILENO);
            dup2(out_fd, STDOUT_FILENO);
            close(out_fd);
        }

        int exit_code = 0;
        int handled = builtin_handle(cmd, &exit_code);
        fflush(stdout);

        if (saved_stdin >= 0) {
            dup2(saved_stdin, STDIN_FILENO);
            close(saved_stdin);
        }
        if (saved_stdout >= 0) {
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);
        }

        if (handled) {
            shell_state_set_last_exit_code(exit_code);
            return;
        }
    }

    if (!background && platform_needs_pty(cmd->cmd) && isatty(STDIN_FILENO)) {
        int code = 0;
        platform_spawn_pty(cmd, &code);
        shell_state_set_last_exit_code(code);
        return;
    }

    int in_fd = resolve_stdin(cmd);
    if (in_fd == -2) {
        shell_state_set_last_exit_code(1);
        return;
    }
    int out_fd = resolve_stdout(cmd);
    if (out_fd == -2) {
        if (in_fd >= 0) {
            close(in_fd);
        }
        shell_state_set_last_exit_code(1);
        return;
    }

    PlatformIO io = {in_fd, out_fd, -1};
    pid_t pid;
    fflush(stdout);
    if (platform_spawn(cmd, io, NULL, 0, &pid) < 0) {
        shell_state_set_last_exit_code(1);
        if (in_fd >= 0) {
            close(in_fd);
        }
        if (out_fd >= 0) {
            close(out_fd);
        }
        return;
    }
    if (in_fd >= 0) {
        close(in_fd);
    }
    if (out_fd >= 0) {
        close(out_fd);
    }

    if (background) {
        printf("[%d]\n", pid);
        return;
    }

    int exit_code_child;
    platform_wait_pid(pid, &exit_code_child);
    shell_state_set_last_exit_code(exit_code_child);
}

static void collect_close_fds(int (*pipes)[2], size_t pipe_count, int keep_a, int keep_b,
                               int **out_fds, size_t *out_count) {
    int *fds = malloc(pipe_count * 2 * sizeof(int));
    size_t count = 0;
    for (size_t i = 0; i < pipe_count; i++) {
        for (int side = 0; side < 2; side++) {
            int fd = pipes[i][side];
            if (fd != keep_a && fd != keep_b) {
                fds[count++] = fd;
            }
        }
    }
    *out_fds = fds;
    *out_count = count;
}

static void run_pipeline_multi(const Pipeline *pipeline) {
    size_t n = pipeline->count;
    int(*pipes)[2] = malloc((n - 1) * sizeof(int[2]));

    for (size_t i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            fprintf(stderr, "fsh: pipe: cannot create pipe\n");
            free(pipes);
            shell_state_set_last_exit_code(1);
            return;
        }
    }

    pid_t *pids = malloc(n * sizeof(pid_t));

    for (size_t i = 0; i < n; i++) {
        const Command *cmd = &pipeline->commands[i];

        int redirect_in_fd = -1;
        int redirect_out_fd = -1;
        int in_fd;
        int out_fd;

        if (i == 0) {
            redirect_in_fd = resolve_stdin(cmd);
            if (redirect_in_fd == -2) {
                redirect_in_fd = -1;
            }
            in_fd = redirect_in_fd;
        } else {
            in_fd = pipes[i - 1][0];
        }

        if (i == n - 1) {
            redirect_out_fd = resolve_stdout(cmd);
            if (redirect_out_fd == -2) {
                redirect_out_fd = -1;
            }
            out_fd = redirect_out_fd;
        } else {
            out_fd = pipes[i][1];
        }

        int *close_fds;
        size_t close_count;
        collect_close_fds(pipes, n - 1, in_fd, out_fd, &close_fds, &close_count);

        PlatformIO io = {in_fd, out_fd, -1};
        fflush(stdout);
        platform_spawn(cmd, io, close_fds, close_count, &pids[i]);
        free(close_fds);

        if (redirect_in_fd >= 0) {
            close(redirect_in_fd);
        }
        if (redirect_out_fd >= 0) {
            close(redirect_out_fd);
        }
    }

    for (size_t i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    free(pipes);

    int last_code = 0;
    for (size_t i = 0; i < n; i++) {
        int code;
        platform_wait_pid(pids[i], &code);
        if (i == n - 1) {
            last_code = code;
        }
    }
    free(pids);

    shell_state_set_last_exit_code(last_code);
}

static void execute_pipeline(const Pipeline *pipeline) {
    if (pipeline->count == 1) {
        run_single(&pipeline->commands[0], pipeline->background);
        return;
    }
    run_pipeline_multi(pipeline);
}

static void execute_statement(Statement *stmt) {
    switch (stmt->kind) {
        case STATEMENT_PIPELINE:
            execute_pipeline(&stmt->pipeline);
            break;
        case STATEMENT_SEQ:
            execute_statement(stmt->left);
            execute_statement(stmt->right);
            break;
        case STATEMENT_AND:
            execute_statement(stmt->left);
            if (shell_state_last_exit_code() == 0) {
                execute_statement(stmt->right);
            }
            break;
        case STATEMENT_OR:
            execute_statement(stmt->left);
            if (shell_state_last_exit_code() != 0) {
                execute_statement(stmt->right);
            }
            break;
    }
}

void executor_run(Statement *stmt) {
    if (!stmt) {
        return;
    }
    execute_statement(stmt);
}
