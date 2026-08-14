#include "fileops/trash.h"

#include "fileops/activity_log.h"
#include "util/json.h"
#include "util/strbuf.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

const char *trash_dir(void) {
    static char path[4096] = {0};
    if (path[0] == '\0') {
        const char *home = getenv("HOME");
        if (!home) {
            home = "~";
        }
        snprintf(path, sizeof(path), "%s/.fsh_trash", home);
    }
    return path;
}

static const char *trash_meta_path(void) {
    static char path[4160] = {0};
    if (path[0] == '\0') {
        snprintf(path, sizeof(path), "%s/.meta.json", trash_dir());
    }
    return path;
}

void trash_ensure_dir(void) {
    struct stat st;
    if (stat(trash_dir(), &st) != 0) {
        mkdir(trash_dir(), 0755);
    }
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

TrashEntry *trash_load_meta(size_t *count) {
    *count = 0;
    JsonValue *root = json_parse_file(trash_meta_path());
    if (!root || root->type != JSON_ARRAY) {
        json_free(root);
        return NULL;
    }

    size_t n = json_array_count(root);
    TrashEntry *entries = malloc((n > 0 ? n : 1) * sizeof(TrashEntry));

    for (size_t i = 0; i < n; i++) {
        const JsonValue *item = json_array_get(root, i);
        TrashEntry *e = &entries[i];
        memset(e, 0, sizeof(*e));
        snprintf(e->id, sizeof(e->id), "%s", json_as_string(json_object_get(item, "id"), ""));
        snprintf(e->name, sizeof(e->name), "%s", json_as_string(json_object_get(item, "name"), ""));
        snprintf(e->original_path, sizeof(e->original_path), "%s",
                 json_as_string(json_object_get(item, "originalPath"), ""));
        e->trashed_at = (long long)json_as_number(json_object_get(item, "trashedAt"), 0);
        e->is_dir = json_as_bool(json_object_get(item, "isDir"), 0);
    }

    *count = n;
    json_free(root);
    return entries;
}

void trash_free_meta(TrashEntry *entries, size_t count) {
    (void)count;
    free(entries);
}

static void save_meta(const TrashEntry *entries, size_t count) {
    StrBuf sb;
    strbuf_init(&sb);
    strbuf_push_char(&sb, '[');

    for (size_t i = 0; i < count; i++) {
        const TrashEntry *e = &entries[i];
        if (i > 0) {
            strbuf_push_char(&sb, ',');
        }
        strbuf_push_str(&sb, "\n  {\"id\":");
        json_write_escaped_string(&sb, e->id);
        strbuf_push_str(&sb, ",\"name\":");
        json_write_escaped_string(&sb, e->name);
        strbuf_push_str(&sb, ",\"originalPath\":");
        json_write_escaped_string(&sb, e->original_path);
        char buf[64];
        snprintf(buf, sizeof(buf), ",\"trashedAt\":%lld,\"isDir\":%s}", e->trashed_at,
                 e->is_dir ? "true" : "false");
        strbuf_push_str(&sb, buf);
    }

    if (count > 0) {
        strbuf_push_char(&sb, '\n');
    }
    strbuf_push_char(&sb, ']');

    FILE *f = fopen(trash_meta_path(), "w");
    if (f) {
        fputs(sb.data, f);
        fclose(f);
    }
    strbuf_free(&sb);
}

static TrashEntry *meta_append(TrashEntry *entries, size_t *count, TrashEntry new_entry) {
    TrashEntry *next = malloc((*count + 1) * sizeof(TrashEntry));
    next[0] = new_entry;
    for (size_t i = 0; i < *count; i++) {
        next[i + 1] = entries[i];
    }
    free(entries);
    *count = *count + 1;
    return next;
}

static TrashEntry *meta_remove_id(TrashEntry *entries, size_t *count, const char *id) {
    size_t write_idx = 0;
    for (size_t i = 0; i < *count; i++) {
        if (strcmp(entries[i].id, id) != 0) {
            entries[write_idx++] = entries[i];
        }
    }
    *count = write_idx;
    return entries;
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

char *trash_move_to_trash(const char *full_path, TrashEntry *out_entry) {
    trash_ensure_dir();

    const char *slash = strrchr(full_path, '/');
    const char *name = slash ? slash + 1 : full_path;

    struct stat st;
    int is_dir = 0;
    if (stat(full_path, &st) == 0) {
        is_dir = S_ISDIR(st.st_mode);
    }

    long long ts = now_millis();
    char id[600];
    snprintf(id, sizeof(id), "%lld_%s", ts, name);

    char dest[4160];
    snprintf(dest, sizeof(dest), "%s/%s", trash_dir(), id);

    if (rename(full_path, dest) != 0) {
        return strdup(strerror(errno));
    }

    TrashEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.id, sizeof(entry.id), "%s", id);
    snprintf(entry.name, sizeof(entry.name), "%s", name);
    snprintf(entry.original_path, sizeof(entry.original_path), "%s", full_path);
    entry.trashed_at = ts;
    entry.is_dir = is_dir;

    size_t count;
    TrashEntry *meta = trash_load_meta(&count);
    meta = meta_append(meta, &count, entry);
    save_meta(meta, count);
    trash_free_meta(meta, count);

    char rel[4096];
    homify(full_path, rel, sizeof(rel));
    log_event("trash", name, rel);

    if (out_entry) {
        *out_entry = entry;
    }
    return NULL;
}

char *trash_restore(const TrashEntry *entry) {
    char src[4160];
    snprintf(src, sizeof(src), "%s/%s", trash_dir(), entry->id);

    struct stat st;
    if (stat(src, &st) != 0) {
        return strdup("File not found in trash");
    }

    char dest[4160];
    snprintf(dest, sizeof(dest), "%s", entry->original_path);
    if (stat(dest, &st) == 0) {
        const char *dot = strrchr(dest, '.');
        const char *slash = strrchr(dest, '/');
        if (dot && (!slash || dot > slash)) {
            size_t base_len = (size_t)(dot - dest);
            char base[4160];
            snprintf(base, sizeof(base), "%.*s", (int)base_len, dest);
            snprintf(dest, sizeof(dest), "%s(restored)%s", base, dot);
        } else {
            char base[4160];
            snprintf(base, sizeof(base), "%s", dest);
            snprintf(dest, sizeof(dest), "%s(restored)", base);
        }
    }

    if (rename(src, dest) != 0) {
        return strdup(strerror(errno));
    }

    size_t count;
    TrashEntry *meta = trash_load_meta(&count);
    meta = meta_remove_id(meta, &count, entry->id);
    save_meta(meta, count);
    trash_free_meta(meta, count);

    char rel_dest[4096];
    homify(dest, rel_dest, sizeof(rel_dest));
    char detail[4200];
    snprintf(detail, sizeof(detail), "restored to %s", rel_dest);
    log_event("restore", entry->name, detail);

    return NULL;
}

char *trash_delete_entry(const TrashEntry *entry) {
    char src[4160];
    snprintf(src, sizeof(src), "%s/%s", trash_dir(), entry->id);

    struct stat st;
    if (stat(src, &st) == 0) {
        if (remove_recursive(src) != 0) {
            return strdup(strerror(errno));
        }
    }

    size_t count;
    TrashEntry *meta = trash_load_meta(&count);
    meta = meta_remove_id(meta, &count, entry->id);
    save_meta(meta, count);
    trash_free_meta(meta, count);

    char detail[4200];
    snprintf(detail, sizeof(detail), "permanently deleted (was at %s)", entry->original_path);
    log_event("delete", entry->name, detail);

    return NULL;
}

char *trash_delete_all(void) {
    size_t count;
    TrashEntry *meta = trash_load_meta(&count);

    for (size_t i = 0; i < count; i++) {
        char src[4160];
        snprintf(src, sizeof(src), "%s/%s", trash_dir(), meta[i].id);
        struct stat st;
        if (stat(src, &st) == 0) {
            remove_recursive(src);
        }
    }

    size_t deleted_count = count;
    save_meta(NULL, 0);
    trash_free_meta(meta, count);

    char label[64];
    snprintf(label, sizeof(label), "%zu item%s", deleted_count, deleted_count == 1 ? "" : "s");
    log_event("empty_trash", label, "trash emptied");

    return NULL;
}
