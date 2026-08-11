#include "core/ast.h"
#include "core/executor.h"
#include "core/parser.h"
#include "env/alias.h"
#include "env/fshrc.h"
#include "input/history.h"
#include "input/line_editor.h"
#include "platform/platform.h"
#include "prompt/prompt.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void handle_sigint(int sig) {
    (void)sig;
}

int main(void) {
    signal(SIGINT, handle_sigint);

    fshrc_load();
    history_load();
    atexit(history_save);

    for (;;) {
        platform_reap_background();

        char *prompt = prompt_build();
        char *line = line_editor_read(prompt);
        free(prompt);

        if (!line) {
            printf("\n");
            break;
        }

        if (line[0] == '\0') {
            free(line);
            continue;
        }

        history_add(line);

        char *expanded = alias_expand_line(line);
        free(line);

        Statement *stmt = parser_parse(expanded);
        free(expanded);

        if (stmt) {
            executor_run(stmt);
            statement_free(stmt);
        }
    }

    return 0;
}
