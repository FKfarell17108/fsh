#ifndef FSH_INPUT_HISTORY_H
#define FSH_INPUT_HISTORY_H

#include <stddef.h>

void history_load(void);
void history_save(void);
void history_add(const char *line);
const char *history_get(size_t index_from_end);
size_t history_count(void);
int history_entry_at(size_t index_from_end, char *cmd_out, size_t cmd_out_size, long long *ts_out);
void history_delete_cmd(const char *cmd);
void history_delete_all(void);

#endif
