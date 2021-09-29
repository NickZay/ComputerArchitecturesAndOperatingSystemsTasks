// -math

#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <stdlib.h>

#define CHECK_OR_EXIT(WHAT_TO_CHECK, ERROR) \
    if (WHAT_TO_CHECK == -1) {              \
        perror(#ERROR);                     \
        exit(errno);                        \
    }

int main() {
    asin(10.0);
    CHECK_OR_EXIT(-1, "math");
}