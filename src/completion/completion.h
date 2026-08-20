#ifndef FSH_COMPLETION_COMPLETION_H
#define FSH_COMPLETION_COMPLETION_H

#include <stddef.h>

typedef struct {
    char **items;
    size_t count;
} CandidateList;

typedef struct {
    CandidateList candidates;
    char partial[4096];
} CompletionResult;

CompletionResult completion_get_candidates(const char *line);
void completion_free(CandidateList *list);
char *completion_common_prefix(const CandidateList *list);
char **completion_tokenize_line(const char *line, size_t *count);
void completion_free_tokens(char **tokens, size_t count);

#endif
