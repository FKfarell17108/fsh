#ifndef FSH_BOOKMARKS_BOOKMARK_PICKER_H
#define FSH_BOOKMARKS_BOOKMARK_PICKER_H

typedef struct {
    int selected;
    char path[4096];
} BookmarkPickerResult;

BookmarkPickerResult bookmark_picker_show(void);

#endif
