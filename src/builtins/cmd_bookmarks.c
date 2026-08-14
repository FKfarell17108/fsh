#include "builtins/builtin.h"

#include "bookmarks/bookmark_picker.h"

#include <stdio.h>
#include <unistd.h>

int cmd_bookmarks(int argc, char **args, int *exit_code) {
    (void)argc;
    (void)args;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        printf("fsh: bookmarks: requires an interactive terminal\n");
        *exit_code = 1;
        return 1;
    }
    BookmarkPickerResult r = bookmark_picker_show();
    if (r.selected) {
        if (chdir(r.path) != 0) { /* best effort */ }
    }
    *exit_code = 0;
    return 1;
}
