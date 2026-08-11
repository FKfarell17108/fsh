#include "builtins/builtin.h"

#include "env/fshrc.h"

#include <stdio.h>
#include <string.h>

#define FSH_VERSION "0.1.0"

int cmd_fshrc(int argc, char **args, int *exit_code) {
    const char *sub = argc > 0 ? args[0] : NULL;

    if (!sub || (strcmp(sub, "init") != 0 && strcmp(sub, "reload") != 0 &&
                 strcmp(sub, "path") != 0 && strcmp(sub, "version") != 0)) {
        printf("\nFSH Configuration Manager\n");
        printf("usage: fshrc <command>\n\n");
        printf(" init     Generate a default .fshrc file\n");
        printf(" reload   Refresh shell configurations\n");
        printf(" path     Show the location of your .fshrc\n");
        printf(" version  Show current fsh version\n\n");
        *exit_code = 0;
        return 1;
    }

    if (strcmp(sub, "init") == 0) {
        FILE *check = fopen(fshrc_path(), "r");
        if (check) {
            fclose(check);
            printf("~/.fshrc already exists. Run 'fshrc reload' to apply changes.\n");
        } else {
            fshrc_generate_default();
            printf("Created ~/.fshrc successfully.\n");
        }
        *exit_code = 0;
        return 1;
    }

    if (strcmp(sub, "reload") == 0) {
        fshrc_load();
        printf("status: fsh reloaded\n");
        *exit_code = 0;
        return 1;
    }

    if (strcmp(sub, "path") == 0) {
        printf("%s\n", fshrc_path());
        *exit_code = 0;
        return 1;
    }

    printf("fsh v%s\n", FSH_VERSION);
    *exit_code = 0;
    return 1;
}
