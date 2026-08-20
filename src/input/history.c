#include "input/history.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define HISTORY_MAX 500

typedef struct {
    char *cmd;
    long long ts;
} HistoryEntry;

static HistoryEntry *g_entries = NULL;
static size_t g_count = 0;
static size_t g_capacity = 0;

static const char *history_path(void) {
    static char path[4096] = {0};
    if (path[0] == '\0') {
        const char *home = getenv("HOME");
        if (!home) {
            home = "~";
        }
        snprintf(path, sizeof(path), "%s/.fsh_history", home);
    }
    return path;
}

static long long current_millis(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void history_remove_at(size_t idx) {
    free(g_entries[idx].cmd);
    for (size_t i = idx; i + 1 < g_count; i++) {
        g_entries[i] = g_entries[i + 1];
    }
    g_count--;
}

static void history_push(const char *cmd, long long ts) {
    for (size_t i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].cmd, cmd) == 0) {
            history_remove_at(i);
            break;
        }
    }

    if (g_count == g_capacity) {
        g_capacity = g_capacity ? g_capacity * 2 : 64;
        g_entries = realloc(g_entries, g_capacity * sizeof(HistoryEntry));
    }
    g_entries[g_count].cmd = strdup(cmd);
    g_entries[g_count].ts = ts;
    g_count++;

    if (g_count > HISTORY_MAX) {
        history_remove_at(0);
    }
}

static int parse_history_line(const char *line, char *cmd_out, size_t cmd_out_size, long long *ts_out) {
    const char *sep = strchr(line, '|');
    if (!sep) {
        *ts_out = 0;
        snprintf(cmd_out, cmd_out_size, "%s", line);
        return 1;
    }

    for (const char *p = line; p < sep; p++) {
        if (!isdigit((unsigned char)*p)) {
            *ts_out = 0;
            snprintf(cmd_out, cmd_out_size, "%s", line);
            return 1;
        }
    }
    if (sep == line) {
        *ts_out = 0;
        snprintf(cmd_out, cmd_out_size, "%s", line);
        return 1;
    }

    *ts_out = atoll(line);
    snprintf(cmd_out, cmd_out_size, "%s", sep + 1);
    return cmd_out[0] != '\0';
}

void history_load(void) {
    FILE *f = fopen(history_path(), "r");
    if (!f) {
        return;
    }
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) {
            continue;
        }

        char cmd[4096];
        long long ts;
        if (parse_history_line(line, cmd, sizeof(cmd), &ts)) {
            history_push(cmd, ts);
        }
    }
    fclose(f);
}

void history_save(void) {
    FILE *f = fopen(history_path(), "w");
    if (!f) {
        return;
    }
    for (size_t i = 0; i < g_count; i++) {
        fprintf(f, "%lld|%s\n", g_entries[i].ts, g_entries[i].cmd);
    }
    fclose(f);
}

void history_add(const char *line) {
    if (g_count > 0 && strcmp(g_entries[g_count - 1].cmd, line) == 0) {
        return;
    }
    history_push(line, current_millis());
}

const char *history_get(size_t index_from_end) {
    if (index_from_end >= g_count) {
        return NULL;
    }
    return g_entries[g_count - 1 - index_from_end].cmd;
}

size_t history_count(void) {
    return g_count;
}

int history_entry_at(size_t index_from_end, char *cmd_out, size_t cmd_out_size, long long *ts_out) {
    if (index_from_end >= g_count) {
        return 0;
    }
    const HistoryEntry *e = &g_entries[g_count - 1 - index_from_end];
    snprintf(cmd_out, cmd_out_size, "%s", e->cmd);
    *ts_out = e->ts;
    return 1;
}

void history_delete_cmd(const char *cmd) {
    for (size_t i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].cmd, cmd) == 0) {
            history_remove_at(i);
            break;
        }
    }
    history_save();
}

void history_delete_all(void) {
    for (size_t i = 0; i < g_count; i++) {
        free(g_entries[i].cmd);
    }
    g_count = 0;
    history_save();
}
