#ifndef FSH_FILEOPS_TRASH_H
#define FSH_FILEOPS_TRASH_H

#include <stddef.h>

typedef struct {
    char id[768];
    char name[512];
    char original_path[4096];
    long long trashed_at;
    int is_dir;
} TrashEntry;

const char *trash_dir(void);
void trash_ensure_dir(void);

TrashEntry *trash_load_meta(size_t *count);
void trash_free_meta(TrashEntry *entries, size_t count);

char *trash_move_to_trash(const char *full_path, TrashEntry *out_entry);
char *trash_restore(const TrashEntry *entry);
char *trash_delete_entry(const TrashEntry *entry);
char *trash_delete_all(void);

#endif
