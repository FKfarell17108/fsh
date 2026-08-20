#ifndef FSH_SEARCH_SEARCH_H
#define FSH_SEARCH_SEARCH_H

typedef struct {
    int has_result;
    char value[8192];
    int did_cd;
} SearchSessionResult;

SearchSessionResult search_show(void);

#endif
