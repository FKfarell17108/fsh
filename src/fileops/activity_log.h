#ifndef FSH_FILEOPS_ACTIVITY_LOG_H
#define FSH_FILEOPS_ACTIVITY_LOG_H

#include <stddef.h>

typedef struct {
    char id[32];
    char kind[32];
    char label[512];
    char detail[512];
    long long ts;
} GeneralEvent;

void activity_log_load(void);
void log_event(const char *kind, const char *label, const char *detail);
void activity_log_delete_command_events(const char *cmd);
void activity_log_delete_all_command_events(void);
const GeneralEvent *activity_log_events(size_t *count);

#endif
