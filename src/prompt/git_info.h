#ifndef FSH_PROMPT_GIT_INFO_H
#define FSH_PROMPT_GIT_INFO_H

#include <stddef.h>

typedef struct {
    int in_repo;
    char branch[256];
    int dirty;
    int staged;
    int untracked;
    int ahead;
    int behind;
} GitInfo;

GitInfo git_info_collect(void);

typedef enum {
    GIT_FILE_MODIFIED,
    GIT_FILE_STAGED,
    GIT_FILE_UNTRACKED,
    GIT_FILE_ADDED,
    GIT_FILE_DELETED,
    GIT_FILE_RENAMED,
    GIT_FILE_CONFLICT
} GitFileStatus;

typedef struct {
    char name[1024];
    GitFileStatus status;
} GitFileEntry;

GitFileEntry *git_file_statuses(const char *dir, size_t *count);
const char *git_status_badge(GitFileStatus status);
void git_status_color_codes(GitFileStatus status, char *out, size_t out_size);

#endif

