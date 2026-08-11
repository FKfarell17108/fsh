#ifndef FSH_PLATFORM_PLATFORM_H
#define FSH_PLATFORM_PLATFORM_H

#include "core/ast.h"

#include <sys/types.h>

typedef struct {
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
} PlatformIO;

int platform_spawn(const Command *cmd, PlatformIO io, const int *close_fds, size_t close_fd_count,
                    pid_t *out_pid);
int platform_wait_pid(pid_t pid, int *exit_code);
void platform_reap_background(void);

int platform_needs_pty(const char *cmd);
int platform_spawn_pty(const Command *cmd, int *exit_code);

int platform_raw_mode_enable(void);
int platform_raw_mode_disable(void);
void platform_get_winsize(int *cols, int *rows);

#endif
