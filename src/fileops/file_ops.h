#ifndef FSH_FILEOPS_FILE_OPS_H
#define FSH_FILEOPS_FILE_OPS_H

#include <stddef.h>

typedef enum { FILEOP_COPY, FILEOP_CUT, FILEOP_RENAME, FILEOP_MOVE } FileOpKind;
typedef enum { FILEOP_PENDING, FILEOP_DONE, FILEOP_ERROR } FileOpStatus;

typedef struct {
    char id[32];
    FileOpKind kind;
    char src_path[4096];
    char src_name[512];
    char dest_path[4096];
    char dest_name[512];
    int is_dir;
    long long timestamp;
    FileOpStatus status;
    char error[512];
} FileOp;

typedef struct {
    char src_path[4096];
    char src_name[512];
    int is_dir;
} ClipboardEntry;

typedef struct {
    int active;
    int is_cut;
    ClipboardEntry *items;
    size_t count;
} Clipboard;

typedef struct {
    int active;
    char **src_paths;
    char **src_names;
    size_t count;
    char label[256];
} MoveMode;

void file_ops_load_log(void);
const FileOp *file_ops_log(size_t *count);

const Clipboard *file_ops_get_clipboard(void);
void file_ops_set_clipboard(int is_cut, const ClipboardEntry *items, size_t count);
void file_ops_clear_clipboard(void);

const MoveMode *file_ops_get_move_mode(void);
void file_ops_set_move_mode(const char *const *src_paths, const char *const *src_names, size_t count,
                             const char *label);
void file_ops_clear_move_mode(void);

char *file_ops_exec_copy(const char *src_full, const char *dest_full);
char *file_ops_exec_move(const char *src_full, const char *dest_full);
char *file_ops_exec_rename(const char *src_full, const char *new_name);
char *file_ops_unique_dest(const char *dest_dir, const char *name);

#endif
