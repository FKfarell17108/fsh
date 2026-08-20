#include "fileops/activity_log.h"

#include "input/history.h"
#include "util/id_gen.h"
#include "util/json.h"
#include "util/strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define MAX_EVENTS 500

static GeneralEvent *g_events = NULL;
static size_t g_count = 0;
static size_t g_capacity = 0;

static const char *log_path(void) {
    static char path[4096] = {0};
    if (path[0] == '\0') {
        const char *home = getenv("HOME");
        if (!home) {
            home = "~";
        }
        snprintf(path, sizeof(path), "%s/.fsh_general_history.json", home);
    }
    return path;
}

static long long now_millis(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void events_clear(void) {
    free(g_events);
    g_events = NULL;
    g_count = 0;
    g_capacity = 0;
}

static void events_push_front_capacity_check(void) {
    if (g_count == g_capacity) {
        g_capacity = g_capacity ? g_capacity * 2 : 64;
        g_events = realloc(g_events, g_capacity * sizeof(GeneralEvent));
    }
}

void activity_log_load(void) {
    events_clear();

    JsonValue *root = json_parse_file(log_path());
    if (!root || root->type != JSON_ARRAY) {
        json_free(root);
        return;
    }

    size_t n = json_array_count(root);
    g_capacity = n > 0 ? n : 1;
    g_events = malloc(g_capacity * sizeof(GeneralEvent));
    g_count = 0;

    for (size_t i = 0; i < n; i++) {
        const JsonValue *item = json_array_get(root, i);
        GeneralEvent ev;
        memset(&ev, 0, sizeof(ev));
        snprintf(ev.id, sizeof(ev.id), "%s", json_as_string(json_object_get(item, "id"), ""));
        snprintf(ev.kind, sizeof(ev.kind), "%s", json_as_string(json_object_get(item, "kind"), ""));
        snprintf(ev.label, sizeof(ev.label), "%s", json_as_string(json_object_get(item, "label"), ""));
        snprintf(ev.detail, sizeof(ev.detail), "%s", json_as_string(json_object_get(item, "detail"), ""));
        ev.ts = (long long)json_as_number(json_object_get(item, "ts"), 0);
        g_events[g_count++] = ev;
    }

    json_free(root);
}

static void persist(void) {
    StrBuf sb;
    strbuf_init(&sb);
    strbuf_push_char(&sb, '[');

    for (size_t i = 0; i < g_count; i++) {
        if (i > 0) {
            strbuf_push_char(&sb, ',');
        }
        strbuf_push_str(&sb, "\n  {\"id\":");
        json_write_escaped_string(&sb, g_events[i].id);
        strbuf_push_str(&sb, ",\"kind\":");
        json_write_escaped_string(&sb, g_events[i].kind);
        strbuf_push_str(&sb, ",\"label\":");
        json_write_escaped_string(&sb, g_events[i].label);
        strbuf_push_str(&sb, ",\"detail\":");
        json_write_escaped_string(&sb, g_events[i].detail);
        char ts_buf[32];
        snprintf(ts_buf, sizeof(ts_buf), ",\"ts\":%lld}", g_events[i].ts);
        strbuf_push_str(&sb, ts_buf);
    }

    if (g_count > 0) {
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

static void remove_matching(int (*match)(const GeneralEvent *, const char *), const char *arg) {
    size_t write_idx = 0;
    for (size_t read_idx = 0; read_idx < g_count; read_idx++) {
        if (!match(&g_events[read_idx], arg)) {
            g_events[write_idx++] = g_events[read_idx];
        }
    }
    g_count = write_idx;
}

static int is_command_with_label(const GeneralEvent *ev, const char *label) {
    return strcmp(ev->kind, "command") == 0 && strcmp(ev->label, label) == 0;
}

static int is_command(const GeneralEvent *ev, const char *unused) {
    (void)unused;
    return strcmp(ev->kind, "command") == 0;
}

void log_event(const char *kind, const char *label, const char *detail) {
    if (strcmp(kind, "command") == 0) {
        remove_matching(is_command_with_label, label);
    }

    events_push_front_capacity_check();
    memmove(g_events + 1, g_events, g_count * sizeof(GeneralEvent));

    GeneralEvent *ev = &g_events[0];
    memset(ev, 0, sizeof(*ev));
    id_gen_short(ev->id, sizeof(ev->id));
    snprintf(ev->kind, sizeof(ev->kind), "%s", kind);
    snprintf(ev->label, sizeof(ev->label), "%s", label);
    snprintf(ev->detail, sizeof(ev->detail), "%s", detail);
    ev->ts = now_millis();
    g_count++;

    if (g_count > MAX_EVENTS) {
        g_count = MAX_EVENTS;
    }

    persist();
}

void activity_log_delete_command_events(const char *cmd) {
    remove_matching(is_command_with_label, cmd);
    persist();
}

void activity_log_delete_all_command_events(void) {
    remove_matching(is_command, NULL);
    persist();
}

static int is_stale_command(const GeneralEvent *ev, const char *unused) {
    (void)unused;
    if (strcmp(ev->kind, "command") != 0) {
        return 0;
    }
    size_t hc = history_count();
    for (size_t i = 0; i < hc; i++) {
        char cmd[4096];
        long long ts;
        if (history_entry_at(i, cmd, sizeof(cmd), &ts) && strcmp(cmd, ev->label) == 0) {
            return 0;
        }
    }
    return 1;
}

void activity_log_prune_stale_commands(void) {
    remove_matching(is_stale_command, NULL);
    persist();
}

const GeneralEvent *activity_log_events(size_t *count) {
    *count = g_count;
    return g_events;
}
