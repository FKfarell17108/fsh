#include "prompt/git_info.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int find_git_root_from(const char *start_dir, char *out, size_t out_size) {
    char path[4096];
    snprintf(path, sizeof(path), "%s", start_dir);

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

static int find_git_root(char *out, size_t out_size) {
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        return 0;
    }
    return find_git_root_from(cwd, out, out_size);
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

static void split_segments(const char *path, char segs[][256], size_t max_segs, size_t *count) {
    *count = 0;
    const char *p = path;
    if (*p == '/') {
        p++;
    }
    while (*p && *count < max_segs) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len > 0 && len < 256) {
            memcpy(segs[*count], p, len);
            segs[*count][len] = '\0';
            (*count)++;
        }
        if (!slash) {
            break;
        }
        p = slash + 1;
    }
}

static void compute_relpath(const char *base, const char *target, char *out, size_t out_size) {
    char base_segs[64][256];
    char target_segs[64][256];
    size_t base_count;
    size_t target_count;
    split_segments(base, base_segs, 64, &base_count);
    split_segments(target, target_segs, 64, &target_count);

    size_t common = 0;
    while (common < base_count && common < target_count && strcmp(base_segs[common], target_segs[common]) == 0) {
        common++;
    }

    out[0] = '\0';
    size_t len = 0;
    for (size_t i = common; i < base_count; i++) {
        if (len > 0 && len + 1 < out_size) {
            out[len++] = '/';
        }
        len += (size_t)snprintf(out + len, out_size - len, "..");
    }
    for (size_t i = common; i < target_count; i++) {
        if (len > 0 && len + 1 < out_size) {
            out[len++] = '/';
            out[len] = '\0';
        }
        len += (size_t)snprintf(out + len, out_size - len, "%s", target_segs[i]);
    }
}

static GitFileStatus classify_status(char x, char y) {
    if (x == '?' && y == '?') {
        return GIT_FILE_UNTRACKED;
    }
    if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D')) {
        return GIT_FILE_CONFLICT;
    }
    if (x == 'R') {
        return GIT_FILE_RENAMED;
    }
    if (x != ' ' && x != '.' && y == ' ') {
        return GIT_FILE_STAGED;
    }
    if (x == 'A' && y != ' ') {
        return GIT_FILE_ADDED;
    }
    if (x == 'D' || y == 'D') {
        return GIT_FILE_DELETED;
    }
    if (y != ' ' && y != '.') {
        return GIT_FILE_MODIFIED;
    }
    return GIT_FILE_STAGED;
}

GitFileEntry *git_file_statuses(const char *dir, size_t *count) {
    *count = 0;
    char root[4096];
    if (!find_git_root_from(dir, root, sizeof(root))) {
        return NULL;
    }

    char *argv[] = {"git", "-C", root, "status", "--porcelain", "-u", NULL};
    char output[65536];
    if (run_capture(argv, output, sizeof(output)) != 0) {
        return NULL;
    }

    GitFileEntry *entries = NULL;
    size_t capacity = 0;

    char *saveptr = NULL;
    char *line = strtok_r(output, "\n", &saveptr);
    while (line) {
        size_t linelen = strlen(line);
        if (linelen >= 4) {
            char x = line[0];
            char y = line[1];
            char file[4096];
            snprintf(file, sizeof(file), "%s", line + 3);
            char *filep = file;
            while (*filep == ' ') {
                filep++;
            }

            char *arrow = strstr(filep, " -> ");
            const char *effective_name = filep;
            if (arrow) {
                effective_name = arrow + 4;
            }

            char abs_path[4096];
            snprintf(abs_path, sizeof(abs_path), "%s/%s", root, effective_name);

            char rel[4096];
            compute_relpath(dir, abs_path, rel, sizeof(rel));

            if (strncmp(rel, "..", 2) == 0 && (rel[2] == '\0' || rel[2] == '/')) {
                line = strtok_r(NULL, "\n", &saveptr);
                continue;
            }

            char top_level[1024];
            char *slash = strchr(rel, '/');
            if (slash) {
                size_t len = (size_t)(slash - rel);
                snprintf(top_level, sizeof(top_level), "%.*s", (int)len, rel);
            } else {
                snprintf(top_level, sizeof(top_level), "%s", rel);
            }

            GitFileStatus status = classify_status(x, y);

            int found_idx = -1;
            for (size_t i = 0; i < *count; i++) {
                if (strcmp(entries[i].name, top_level) == 0) {
                    found_idx = (int)i;
                    break;
                }
            }
            if (found_idx < 0) {
                if (*count == capacity) {
                    capacity = capacity ? capacity * 2 : 32;
                    entries = realloc(entries, capacity * sizeof(GitFileEntry));
                }
                snprintf(entries[*count].name, sizeof(entries[*count].name), "%s", top_level);
                entries[*count].status = status;
                (*count)++;
            } else {
                if (status == GIT_FILE_CONFLICT) {
                    entries[found_idx].status = GIT_FILE_CONFLICT;
                } else if (status == GIT_FILE_STAGED && entries[found_idx].status != GIT_FILE_CONFLICT) {
                    entries[found_idx].status = GIT_FILE_STAGED;
                }
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }

    return entries;
}

const char *git_status_badge(GitFileStatus status) {
    switch (status) {
        case GIT_FILE_MODIFIED: return "~";
        case GIT_FILE_STAGED: return "+";
        case GIT_FILE_ADDED: return "+";
        case GIT_FILE_UNTRACKED: return "?";
        case GIT_FILE_DELETED: return "-";
        case GIT_FILE_RENAMED: return "\xe2\x86\x92";
        case GIT_FILE_CONFLICT: return "!";
    }
    return "";
}

void git_status_color_codes(GitFileStatus status, char *out, size_t out_size) {
    const char *hex;
    switch (status) {
        case GIT_FILE_MODIFIED: hex = "#FFD580"; break;
        case GIT_FILE_STAGED: hex = "#AEDD87"; break;
        case GIT_FILE_ADDED: hex = "#AEDD87"; break;
        case GIT_FILE_UNTRACKED: hex = "#FF9E64"; break;
        case GIT_FILE_DELETED: hex = "#FF7B8A"; break;
        case GIT_FILE_RENAMED: hex = "#70D4FF"; break;
        case GIT_FILE_CONFLICT: hex = "#FF5370"; break;
        default: hex = "#FFFFFF"; break;
    }
    unsigned int r;
    unsigned int g;
    unsigned int b;
    sscanf(hex, "#%02x%02x%02x", &r, &g, &b);
    snprintf(out, out_size, "\x1b[38;2;%u;%u;%um", r, g, b);
}
