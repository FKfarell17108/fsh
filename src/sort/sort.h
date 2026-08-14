#ifndef FSH_SORT_SORT_H
#define FSH_SORT_SORT_H

#include <stddef.h>
#include <time.h>

typedef enum { SORT_ASC, SORT_DESC } SortDir;

typedef enum { LS_SORT_NAME, LS_SORT_TYPE, LS_SORT_SIZE, LS_SORT_DATE, LS_SORT_HIDDEN } LsSortKey;
typedef enum { TRASH_SORT_DATE, TRASH_SORT_NAME, TRASH_SORT_SIZE, TRASH_SORT_TYPE } TrashSortKey;
typedef enum { LOG_SORT_DATE, LOG_SORT_KIND, LOG_SORT_STATUS } LogSortKey;

typedef struct {
    LsSortKey key;
    SortDir dir;
} LsSort;

typedef struct {
    TrashSortKey key;
    SortDir dir;
} TrashSort;

typedef struct {
    LogSortKey key;
    SortDir dir;
} LogSort;

extern const LsSort DEFAULT_LS_SORT;
extern const TrashSort DEFAULT_TRASH_SORT;
extern const LogSort DEFAULT_LOG_SORT;

typedef struct {
    char name[1024];
    int is_dir;
    int hidden;
    long long size;
    long long mtime_ms;
} SortableEntry;

void sort_ls_entries(SortableEntry *entries, size_t count, LsSort sort);

typedef struct {
    int picked;
    int key;
    SortDir dir;
} SortPickResult;

SortPickResult sort_show_picker(const char *kind, int current_key, SortDir current_dir, int anchor_row);

const char *sort_ls_label(LsSort s);

#endif
