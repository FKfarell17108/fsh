#ifndef FSH_INPUT_HISTORY_MANAGER_H
#define FSH_INPUT_HISTORY_MANAGER_H

typedef struct {
    int selected;
    char cmd[4096];
} HistoryManagerResult;

HistoryManagerResult history_manager_show(void);

#endif
