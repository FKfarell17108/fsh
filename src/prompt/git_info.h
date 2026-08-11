#ifndef FSH_PROMPT_GIT_INFO_H
#define FSH_PROMPT_GIT_INFO_H

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

#endif
