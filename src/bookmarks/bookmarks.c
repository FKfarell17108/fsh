#include "bookmarks/bookmarks.h"

#include "util/id_gen.h"
#include "util/json.h"
#include "util/strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static Bookmark *g_bookmarks = NULL;
static size_t g_count = 0;
static size_t g_capacity = 0;

static const char *bookmarks_path(void) {
    static char path[4096] = {0};
    if (path[0] == '\0') {
        const char *home = getenv("HOME");
        if (!home) {
            home = "~";
        }
        snprintf(path, sizeof(path), "%s/.fsh_bookmarks.json", home);
    }
    return path;
}

static long long now_millis(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static const char *basename_of(const char *p) {
    const char *slash = strrchr(p, '/');
    return slash ? slash + 1 : p;
}

void bookmarks_homify(const char *p, char *out, size_t out_size) {
    const char *home = getenv("HOME");
    if (home && home[0] != '\0' && strncmp(p, home, strlen(home)) == 0) {
        snprintf(out, out_size, "~%s", p + strlen(home));
    } else {
        snprintf(out, out_size, "%s", p);
    }
}

static void persist(void) {
    StrBuf sb;
    strbuf_init(&sb);
    strbuf_push_char(&sb, '[');

    for (size_t i = 0; i < g_count; i++) {
        const Bookmark *b = &g_bookmarks[i];
        if (i > 0) {
            strbuf_push_char(&sb, ',');
        }
        strbuf_push_str(&sb, "\n  {\"id\":");
        json_write_escaped_string(&sb, b->id);
        strbuf_push_str(&sb, ",\"name\":");
        json_write_escaped_string(&sb, b->name);
        strbuf_push_str(&sb, ",\"fullPath\":");
        json_write_escaped_string(&sb, b->full_path);
        char buf[32];
        snprintf(buf, sizeof(buf), ",\"addedAt\":%lld}", b->added_at);
        strbuf_push_str(&sb, buf);
    }

    if (g_count > 0) {
        strbuf_push_char(&sb, '\n');
    }
    strbuf_push_char(&sb, ']');

    FILE *f = fopen(bookmarks_path(), "w");
    if (f) {
        fputs(sb.data, f);
        fclose(f);
    }
    strbuf_free(&sb);
}

void bookmarks_load(void) {
    free(g_bookmarks);
    g_bookmarks = NULL;
    g_count = 0;
    g_capacity = 0;

    JsonValue *root = json_parse_file(bookmarks_path());
    if (!root || root->type != JSON_ARRAY) {
        json_free(root);
        return;
    }

    size_t n = json_array_count(root);
    g_capacity = n > 0 ? n : 1;
    g_bookmarks = malloc(g_capacity * sizeof(Bookmark));

    for (size_t i = 0; i < n; i++) {
        const JsonValue *item = json_array_get(root, i);
        Bookmark *b = &g_bookmarks[g_count];
        memset(b, 0, sizeof(*b));
        snprintf(b->id, sizeof(b->id), "%s", json_as_string(json_object_get(item, "id"), ""));
        snprintf(b->name, sizeof(b->name), "%s", json_as_string(json_object_get(item, "name"), ""));
        snprintf(b->full_path, sizeof(b->full_path), "%s", json_as_string(json_object_get(item, "fullPath"), ""));
        b->added_at = (long long)json_as_number(json_object_get(item, "addedAt"), 0);
        g_count++;
    }

    json_free(root);
}

const Bookmark *bookmarks_get(size_t *count) {
    *count = g_count;
    return g_bookmarks;
}

int bookmarks_is_bookmarked(const char *full_path) {
    for (size_t i = 0; i < g_count; i++) {
        if (strcmp(g_bookmarks[i].full_path, full_path) == 0) {
            return 1;
        }
    }
    return 0;
}

const Bookmark *bookmarks_add(const char *full_path) {
    if (bookmarks_is_bookmarked(full_path)) {
        return NULL;
    }

    if (g_count == g_capacity) {
        g_capacity = g_capacity ? g_capacity * 2 : 8;
        g_bookmarks = realloc(g_bookmarks, g_capacity * sizeof(Bookmark));
    }

    memmove(g_bookmarks + 1, g_bookmarks, g_count * sizeof(Bookmark));
    Bookmark *b = &g_bookmarks[0];
    memset(b, 0, sizeof(*b));
    id_gen_short(b->id, sizeof(b->id));
    const char *name = basename_of(full_path);
    snprintf(b->name, sizeof(b->name), "%s", name[0] != '\0' ? name : full_path);
    snprintf(b->full_path, sizeof(b->full_path), "%s", full_path);
    b->added_at = now_millis();
    g_count++;

    persist();
    return b;
}

int bookmarks_remove(const char *full_path) {
    size_t write_idx = 0;
    int removed = 0;
    for (size_t i = 0; i < g_count; i++) {
        if (strcmp(g_bookmarks[i].full_path, full_path) == 0) {
            removed = 1;
            continue;
        }
        g_bookmarks[write_idx++] = g_bookmarks[i];
    }
    g_count = write_idx;
    if (removed) {
        persist();
    }
    return removed;
}

int bookmarks_remove_by_id(const char *id) {
    size_t write_idx = 0;
    int removed = 0;
    for (size_t i = 0; i < g_count; i++) {
        if (strcmp(g_bookmarks[i].id, id) == 0) {
            removed = 1;
            continue;
        }
        g_bookmarks[write_idx++] = g_bookmarks[i];
    }
    g_count = write_idx;
    if (removed) {
        persist();
    }
    return removed;
}

const char *bookmarks_toggle(const char *full_path) {
    if (bookmarks_is_bookmarked(full_path)) {
        bookmarks_remove(full_path);
        return "removed";
    }
    bookmarks_add(full_path);
    return "added";
}
