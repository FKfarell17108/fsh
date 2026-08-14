#ifndef FSH_BOOKMARKS_BOOKMARKS_H
#define FSH_BOOKMARKS_BOOKMARKS_H

#include <stddef.h>

typedef struct {
    char id[32];
    char name[512];
    char full_path[4096];
    long long added_at;
} Bookmark;

void bookmarks_load(void);
const Bookmark *bookmarks_get(size_t *count);
int bookmarks_is_bookmarked(const char *full_path);
const Bookmark *bookmarks_add(const char *full_path);
int bookmarks_remove(const char *full_path);
int bookmarks_remove_by_id(const char *id);
const char *bookmarks_toggle(const char *full_path);
void bookmarks_homify(const char *p, char *out, size_t out_size);

#endif
