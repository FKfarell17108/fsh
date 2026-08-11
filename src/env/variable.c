#include "env/variable.h"

#include "env/shell_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *variable_lookup(const char *name) {
    if (strcmp(name, "?") == 0) {
        static char buf[16];
        snprintf(buf, sizeof(buf), "%d", shell_state_last_exit_code());
        return buf;
    }
    return getenv(name);
}
