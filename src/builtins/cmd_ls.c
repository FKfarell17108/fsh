#include "builtins/builtin.h"

#include "browser/interactive_browser.h"

#include <unistd.h>

int cmd_ls(int argc, char **args, int *exit_code) {
    (void)args;
    if (argc == 0 && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        BrowserResult r = interactive_ls();
        if (r.kind == BROWSER_RESULT_CD) {
            if (chdir(r.path) != 0) { /* best effort */ }
        }
        *exit_code = 0;
        return 1;
    }
    return 0;
}
