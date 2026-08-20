#include "platform/platform.h"

#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static struct termios g_orig_termios;
static int g_raw_depth = 0;

int platform_raw_mode_enable(void) {
    if (g_raw_depth > 0) {
        g_raw_depth++;
        return 0;
    }
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) < 0) {
        return -1;
    }

    struct termios raw = g_orig_termios;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(tcflag_t)(OPOST);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) {
        return -1;
    }
    g_raw_depth = 1;
    return 0;
}

int platform_raw_mode_disable(void) {
    if (g_raw_depth == 0) {
        return 0;
    }
    g_raw_depth--;
    if (g_raw_depth > 0) {
        return 0;
    }
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios) < 0) {
        return -1;
    }
    return 0;
}

void platform_get_winsize(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    } else {
        *cols = 80;
        *rows = 24;
    }
}
