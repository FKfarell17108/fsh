#include "env/shell_state.h"

static int g_last_exit_code = 0;

int shell_state_last_exit_code(void) {
    return g_last_exit_code;
}

void shell_state_set_last_exit_code(int code) {
    g_last_exit_code = code;
}
