#ifndef FSH_COMPLETION_COMPLETION_PICKER_H
#define FSH_COMPLETION_COMPLETION_PICKER_H

#include "completion/completion.h"

typedef enum { COMPLETION_PICK_NONE, COMPLETION_PICK_SELECTED, COMPLETION_PICK_HISTORY } CompletionPickKind;

typedef struct {
    CompletionPickKind kind;
    char chosen[4096];
} CompletionPickResult;

CompletionPickResult completion_picker_show(const CandidateList *candidates);

#endif
