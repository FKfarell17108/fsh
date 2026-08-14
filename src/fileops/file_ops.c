#include "fileops/file_ops.h"

#include "fileops/activity_log.h"
#include "util/id_gen.h"
#include "util/json.h"
#include "util/strbuf.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_OPS 200

static FileOp *g_ops = NULL;
static size_t g_op_count = 0;
static size_t g_op_capacity = 0;

static Clipboard g_clipboard = {0};
static MoveMode g_move_mode = {0};

static const char *log_path(void) {
    static char path[4096] = {0};
    if (path[0] == '\0') {
        const char *home = getenv("HOME");
        if (!home) {
            home = "~";
        }
        snprintf(path, sizeof(path), "%s/.fsh_fileops.json", home);
    }
    return path;
}

static long long now_millis(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void homify(const char *p, char *out, size_t out_size) {
    const char *home = getenv("HOME");
    if (home && home[0] != '\0' && strncmp(p, home, strlen(home)) == 0) {
        snprintf(out, out_size, "~%s", p + strlen(home));
    } else {
        snprintf(out, out_size, "%s", p);
    }
}

static const char *path_basename_dup(const char *path, char *out, size_t out_size) {
    const char *slash = strrchr(path, '/');
    snprintf(out, out_size, "%s", slash ? slash + 1 : path);
    return out;
}

static void path_dirname_dup(const char *path, char *out, size_t out_size) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        snprintf(out, out_size, ".");
        return;
    }
    if (slash == path) {
        snprintf(out, out_size, "/");
        return;
    }
    size_t len = (size_t)(slash - path);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, path, len);
    out[len] = '\0';
}

static const char *kind_to_str(FileOpKind k) {
    switch (k) {
        case FILEOP_COPY: return "copy";
        case FILEOP_CUT: return "cut";
        case FILEOP_RENAME: return "rename";
        case FILEOP_MOVE: return "move";
    }
    return "copy";
}

static const char *status_to_str(FileOpStatus s) {
    switch (s) {
        case FILEOP_PENDING: return "pending";
        case FILEOP_DONE: return "done";
        case FILEOP_ERROR: return "error";
    }
    return "pending";
}

static void persist_log(void) {
    StrBuf sb;
    strbuf_init(&sb);
    strbuf_push_char(&sb, '[');

    for (size_t i = 0; i < g_op_count; i++) {
        const FileOp *op = &g_ops[i];
        if (i > 0) {
            strbuf_push_char(&sb, ',');
        }
        strbuf_push_str(&sb, "\n  {\"id\":");
        json_write_escaped_string(&sb, op->id);
        strbuf_push_str(&sb, ",\"kind\":");
        json_write_escaped_string(&sb, kind_to_str(op->kind));
        strbuf_push_str(&sb, ",\"srcPath\":");
        json_write_escaped_string(&sb, op->src_path);
        strbuf_push_str(&sb, ",\"srcName\":");
        json_write_escaped_string(&sb, op->src_name);
        strbuf_push_str(&sb, ",\"destPath\":");
        json_write_escaped_string(&sb, op->dest_path);
        strbuf_push_str(&sb, ",\"destName\":");
        json_write_escaped_string(&sb, op->dest_name);
        char buf[64];
        snprintf(buf, sizeof(buf), ",\"isDir\":%s,\"timestamp\":%lld,\"status\":",
                 op->is_dir ? "true" : "false", op->timestamp);
        strbuf_push_str(&sb, buf);
        json_write_escaped_string(&sb, status_to_str(op->status));
        if (op->error[0] != '\0') {
            strbuf_push_str(&sb, ",\"error\":");
            json_write_escaped_string(&sb, op->error);
        }
        strbuf_push_str(&sb, "}");
    }

    if (g_op_count > 0) {
        strbuf_push_char(&sb, '\n');
    }
    strbuf_push_char(&sb, ']');

    FILE *f = fopen(log_path(), "w");
    if (f) {
        fputs(sb.data, f);
        fclose(f);
    }
    strbuf_free(&sb);
}

void file_ops_load_log(void) {
    free(g_ops);
    g_ops = NULL;
    g_op_count = 0;
    g_op_capacity = 0;

    JsonValue *root = json_parse_file(log_path());
    if (!root || root->type != JSON_ARRAY) {
        json_free(root);
        return;
    }

    size_t n = json_array_count(root);
    g_op_capacity = n > 0 ? n : 1;
    g_ops = malloc(g_op_capacity * sizeof(FileOp));

    for (size_t i = 0; i < n; i++) {
        const JsonValue *item = json_array_get(root, i);
        FileOp op;
        memset(&op, 0, sizeof(op));
        snprintf(op.id, sizeof(op.id), "%s", json_as_string(json_object_get(item, "id"), ""));

        const char *kind_s = json_as_string(json_object_get(item, "kind"), "copy");
        if (strcmp(kind_s, "cut") == 0) op.kind = FILEOP_CUT;
        else if (strcmp(kind_s, "rename") == 0) op.kind = FILEOP_RENAME;
        else if (strcmp(kind_s, "move") == 0) op.kind = FILEOP_MOVE;
        else op.kind = FILEOP_COPY;

        snprintf(op.src_path, sizeof(op.src_path), "%s", json_as_string(json_object_get(item, "srcPath"), ""));
        snprintf(op.src_name, sizeof(op.src_name), "%s", json_as_string(json_object_get(item, "srcName"), ""));
        snprintf(op.dest_path, sizeof(op.dest_path), "%s", json_as_string(json_object_get(item, "destPath"), ""));
        snprintf(op.dest_name, sizeof(op.dest_name), "%s", json_as_string(json_object_get(item, "destName"), ""));
        op.is_dir = json_as_bool(json_object_get(item, "isDir"), 0);
        op.timestamp = (long long)json_as_number(json_object_get(item, "timestamp"), 0);

        const char *status_s = json_as_string(json_object_get(item, "status"), "pending");
        if (strcmp(status_s, "done") == 0) op.status = FILEOP_DONE;
        else if (strcmp(status_s, "error") == 0) op.status = FILEOP_ERROR;
        else op.status = FILEOP_PENDING;

        snprintf(op.error, sizeof(op.error), "%s", json_as_string(json_object_get(item, "error"), ""));

        g_ops[g_op_count++] = op;
    }

    json_free(root);
}

const FileOp *file_ops_log(size_t *count) {
    *count = g_op_count;
    return g_ops;
}

static void push_op(FileOp op) {
    if (g_op_count == g_op_capacity) {
        g_op_capacity = g_op_capacity ? g_op_capacity * 2 : 32;
        g_ops = realloc(g_ops, g_op_capacity * sizeof(FileOp));
    }
    memmove(g_ops + 1, g_ops, g_op_count * sizeof(FileOp));
    g_ops[0] = op;
    g_op_count++;
    if (g_op_count > MAX_OPS) {
        g_op_count = MAX_OPS;
    }
    persist_log();
}

const Clipboard *file_ops_get_clipboard(void) {
    return &g_clipboard;
}

void file_ops_set_clipboard(int is_cut, const ClipboardEntry *items, size_t count) {
    free(g_clipboard.items);
    g_clipboard.items = malloc(count * sizeof(ClipboardEntry));
    memcpy(g_clipboard.items, items, count * sizeof(ClipboardEntry));
    g_clipboard.count = count;
    g_clipboard.is_cut = is_cut;
    g_clipboard.active = 1;
}

void file_ops_clear_clipboard(void) {
    free(g_clipboard.items);
    g_clipboard.items = NULL;
    g_clipboard.count = 0;
    g_clipboard.active = 0;
}

const MoveMode *file_ops_get_move_mode(void) {
    return &g_move_mode;
}

void file_ops_set_move_mode(const char *const *src_paths, const char *const *src_names, size_t count,
                             const char *label) {
    for (size_t i = 0; i < g_move_mode.count; i++) {
        free(g_move_mode.src_paths[i]);
        free(g_move_mode.src_names[i]);
    }
    free(g_move_mode.src_paths);
    free(g_move_mode.src_names);

    g_move_mode.src_paths = malloc(count * sizeof(char *));
    g_move_mode.src_names = malloc(count * sizeof(char *));
    for (size_t i = 0; i < count; i++) {
        g_move_mode.src_paths[i] = strdup(src_paths[i]);
        g_move_mode.src_names[i] = strdup(src_names[i]);
    }
    g_move_mode.count = count;
    snprintf(g_move_mode.label, sizeof(g_move_mode.label), "%s", label);
    g_move_mode.active = 1;
}

void file_ops_clear_move_mode(void) {
    for (size_t i = 0; i < g_move_mode.count; i++) {
        free(g_move_mode.src_paths[i]);
        free(g_move_mode.src_names[i]);
    }
    free(g_move_mode.src_paths);
    free(g_move_mode.src_names);
    g_move_mode.src_paths = NULL;
    g_move_mode.src_names = NULL;
    g_move_mode.count = 0;
    g_move_mode.active = 0;
}

static int mkdir_p(const char *path) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static int copy_file_bytes(const char *src, const char *dest) {
    int in_fd = open(src, O_RDONLY);
    if (in_fd < 0) {
        return -1;
    }
    struct stat st;
    fstat(in_fd, &st);

    int out_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0777);
    if (out_fd < 0) {
        close(in_fd);
        return -1;
    }

    char buf[65536];
    ssize_t n;
    while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(out_fd, buf + written, (size_t)(n - written));
            if (w < 0) {
                close(in_fd);
                close(out_fd);
                return -1;
            }
            written += w;
        }
    }

    close(in_fd);
    close(out_fd);
    return n < 0 ? -1 : 0;
}

static int copy_recursive(const char *src, const char *dest) {
    struct stat st;
    if (stat(src, &st) != 0) {
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (mkdir_p(dest) != 0) {
            return -1;
        }
        DIR *dp = opendir(src);
        if (!dp) {
            return -1;
        }
        struct dirent *entry;
        int rc = 0;
        while ((entry = readdir(dp)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            char child_src[4096];
            char child_dest[4096];
            snprintf(child_src, sizeof(child_src), "%s/%s", src, entry->d_name);
            snprintf(child_dest, sizeof(child_dest), "%s/%s", dest, entry->d_name);
            if (copy_recursive(child_src, child_dest) != 0) {
                rc = -1;
            }
        }
        closedir(dp);
        return rc;
    }

    char dest_dir[4096];
    path_dirname_dup(dest, dest_dir, sizeof(dest_dir));
    if (mkdir_p(dest_dir) != 0) {
        return -1;
    }
    return copy_file_bytes(src, dest);
}

static int remove_recursive(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR *dp = opendir(path);
        if (!dp) {
            return -1;
        }
        struct dirent *entry;
        int rc = 0;
        while ((entry = readdir(dp)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            char child[4096];
            snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
            if (remove_recursive(child) != 0) {
                rc = -1;
            }
        }
        closedir(dp);
        if (rmdir(path) != 0) {
            rc = -1;
        }
        return rc;
    }
    return unlink(path) == 0 ? 0 : -1;
}

char *file_ops_exec_copy(const char *src_full, const char *dest_full) {
    struct stat st;
    int is_dir = 0;
    if (stat(src_full, &st) == 0) {
        is_dir = S_ISDIR(st.st_mode);
    }

    FileOp op;
    memset(&op, 0, sizeof(op));
    id_gen_short(op.id, sizeof(op.id));
    op.kind = FILEOP_COPY;
    snprintf(op.src_path, sizeof(op.src_path), "%s", src_full);
    path_basename_dup(src_full, op.src_name, sizeof(op.src_name));
    snprintf(op.dest_path, sizeof(op.dest_path), "%s", dest_full);
    path_basename_dup(dest_full, op.dest_name, sizeof(op.dest_name));
    op.is_dir = is_dir;
    op.timestamp = now_millis();

    if (copy_recursive(src_full, dest_full) == 0) {
        op.status = FILEOP_DONE;
        push_op(op);

        char hs[4096];
        char hd[4096];
        homify(src_full, hs, sizeof(hs));
        homify(dest_full, hd, sizeof(hd));
        char detail[8320];
        snprintf(detail, sizeof(detail), "%s  \xe2\x86\x92  %s", hs, hd);
        log_event("copy", op.src_name, detail);
        return NULL;
    }

    op.status = FILEOP_ERROR;
    snprintf(op.error, sizeof(op.error), "%s", strerror(errno));
    push_op(op);
    return strdup(op.error);
}

char *file_ops_exec_move(const char *src_full, const char *dest_full) {
    struct stat st;
    int is_dir = 0;
    if (stat(src_full, &st) == 0) {
        is_dir = S_ISDIR(st.st_mode);
    }

    FileOp op;
    memset(&op, 0, sizeof(op));
    id_gen_short(op.id, sizeof(op.id));
    op.kind = FILEOP_MOVE;
    snprintf(op.src_path, sizeof(op.src_path), "%s", src_full);
    path_basename_dup(src_full, op.src_name, sizeof(op.src_name));
    snprintf(op.dest_path, sizeof(op.dest_path), "%s", dest_full);
    path_basename_dup(dest_full, op.dest_name, sizeof(op.dest_name));
    op.is_dir = is_dir;
    op.timestamp = now_millis();

    int ok = rename(src_full, dest_full) == 0;
    if (!ok) {
        if (copy_recursive(src_full, dest_full) == 0 && remove_recursive(src_full) == 0) {
            ok = 1;
        }
    }

    if (ok) {
        op.status = FILEOP_DONE;
        push_op(op);

        char hs[4096];
        char hd[4096];
        homify(src_full, hs, sizeof(hs));
        homify(dest_full, hd, sizeof(hd));
        char detail[8320];
        snprintf(detail, sizeof(detail), "%s  \xe2\x86\x92  %s", hs, hd);
        log_event("move", op.src_name, detail);
        return NULL;
    }

    op.status = FILEOP_ERROR;
    snprintf(op.error, sizeof(op.error), "%s", strerror(errno));
    push_op(op);
    return strdup(op.error);
}

char *file_ops_exec_rename(const char *src_full, const char *new_name) {
    char src_dir[4096];
    path_dirname_dup(src_full, src_dir, sizeof(src_dir));
    char dest_full[4608];
    snprintf(dest_full, sizeof(dest_full), "%s/%s", src_dir, new_name);

    struct stat st;
    int is_dir = 0;
    if (stat(src_full, &st) == 0) {
        is_dir = S_ISDIR(st.st_mode);
    }

    FileOp op;
    memset(&op, 0, sizeof(op));
    id_gen_short(op.id, sizeof(op.id));
    op.kind = FILEOP_RENAME;
    snprintf(op.src_path, sizeof(op.src_path), "%s", src_full);
    path_basename_dup(src_full, op.src_name, sizeof(op.src_name));
    snprintf(op.dest_path, sizeof(op.dest_path), "%s", dest_full);
    snprintf(op.dest_name, sizeof(op.dest_name), "%s", new_name);
    op.is_dir = is_dir;
    op.timestamp = now_millis();

    if (rename(src_full, dest_full) == 0) {
        op.status = FILEOP_DONE;
        push_op(op);

        char hdir[4096];
        homify(src_dir, hdir, sizeof(hdir));
        char detail[8320];
        snprintf(detail, sizeof(detail), "%s  \xe2\x86\x92  %s  (in %s)", op.src_name, new_name, hdir);
        log_event("rename", op.src_name, detail);
        return NULL;
    }

    op.status = FILEOP_ERROR;
    snprintf(op.error, sizeof(op.error), "%s", strerror(errno));
    push_op(op);
    return strdup(op.error);
}

char *file_ops_unique_dest(const char *dest_dir, const char *name) {
    const char *dot = strrchr(name, '.');
    char base[512];
    char ext[128];
    if (dot && dot != name) {
        size_t base_len = (size_t)(dot - name);
        snprintf(base, sizeof(base), "%.*s", (int)base_len, name);
        snprintf(ext, sizeof(ext), "%s", dot);
    } else {
        snprintf(base, sizeof(base), "%s", name);
        ext[0] = '\0';
    }

    char candidate[4096];
    snprintf(candidate, sizeof(candidate), "%s/%s", dest_dir, name);

    struct stat st;
    int i = 1;
    while (stat(candidate, &st) == 0) {
        snprintf(candidate, sizeof(candidate), "%s/%s (%d)%s", dest_dir, base, i, ext);
        i++;
    }

    return strdup(candidate);
}
