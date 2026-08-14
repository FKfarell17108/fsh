#include "util/id_gen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static const char BASE36[] = "0123456789abcdefghijklmnopqrstuvwxyz";

static void to_base36(long long value, char *out, size_t out_size) {
    char tmp[32];
    size_t i = 0;
    if (value == 0) {
        snprintf(out, out_size, "0");
        return;
    }
    while (value > 0 && i < sizeof(tmp)) {
        tmp[i++] = BASE36[value % 36];
        value /= 36;
    }
    size_t j = 0;
    while (i > 0 && j + 1 < out_size) {
        out[j++] = tmp[--i];
    }
    out[j] = '\0';
}

void id_gen_short(char *out, size_t out_size) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long millis = (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    static int seeded = 0;
    if (!seeded) {
        srandom((unsigned int)(millis ^ (long long)getpid()));
        seeded = 1;
    }

    char time_part[32];
    to_base36(millis, time_part, sizeof(time_part));

    char rand_part[8];
    for (int i = 0; i < 4; i++) {
        rand_part[i] = BASE36[random() % 36];
    }
    rand_part[4] = '\0';

    snprintf(out, out_size, "%s%s", time_part, rand_part);
}
