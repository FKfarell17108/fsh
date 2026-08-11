#include "platform/platform.h"

#include <errno.h>
#include <pty.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static const char *PTY_COMMANDS[] = {
    "vim", "vi", "nvim", "nano", "emacs", "micro", "helix", "hx",
    "htop", "btop", "top", "atop",
    "less", "more", "man",
    "fzf", "ranger", "nnn", "mc",
    "ssh", "ssh-keygen", "ssh-add", "scp", "sftp", "tmux", "screen",
    "python", "python3", "node", "irb", "ghci", "lua",
    "bash", "zsh", "fish", "sh",
    "git", "sudo", "su",
    NULL
};

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int platform_needs_pty(const char *cmd) {
    const char *base = base_name(cmd);
    for (int i = 0; PTY_COMMANDS[i] != NULL; i++) {
        if (strcmp(base, PTY_COMMANDS[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int platform_spawn_pty(const Command *cmd, int *exit_code) {
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) {
        ws.ws_col = 80;
        ws.ws_row = 24;
    }

    struct termios orig;
    tcgetattr(STDIN_FILENO, &orig);

    int master_fd;
    pid_t pid = forkpty(&master_fd, NULL, &orig, &ws);
    if (pid < 0) {
        *exit_code = 1;
        return -1;
    }

    if (pid == 0) {
        char **argv = malloc((cmd->argc + 2) * sizeof(char *));
        argv[0] = cmd->cmd;
        for (size_t i = 0; i < cmd->argc; i++) {
            argv[i + 1] = cmd->args[i];
        }
        argv[cmd->argc + 1] = NULL;
        execvp(cmd->cmd, argv);
        fprintf(stderr, "fsh: %s: command not found\n", cmd->cmd);
        _exit(127);
    }

    struct termios raw = orig;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    char buf[4096];
    int status = 0;
    int running = 1;

    while (running) {
        struct pollfd fds[2];
        fds[0].fd = STDIN_FILENO;
        fds[0].events = POLLIN;
        fds[1].fd = master_fd;
        fds[1].events = POLLIN;

        int ready = poll(fds, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (fds[0].revents & POLLIN) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n > 0) {
                if (write(master_fd, buf, (size_t)n) < 0) {
                    running = 0;
                }
            }
        }

        if (fds[1].revents & (POLLIN | POLLHUP)) {
            ssize_t n = read(master_fd, buf, sizeof(buf));
            if (n > 0) {
                if (write(STDOUT_FILENO, buf, (size_t)n) < 0) {
                    running = 0;
                }
            } else {
                running = 0;
            }
        }

        if (waitpid(pid, &status, WNOHANG) == pid) {
            running = 0;
        }
    }

    waitpid(pid, &status, 0);
    tcsetattr(STDIN_FILENO, TCSANOW, &orig);
    close(master_fd);

    if (WIFEXITED(status)) {
        *exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        *exit_code = 128 + WTERMSIG(status);
    } else {
        *exit_code = 1;
    }

    return 0;
}
