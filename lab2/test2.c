#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    int n = atoi(argv[1]);

    int accum = 0;
    for (int i = 0; i < n; i++) {
        accum += n;
        printf("%d\n", accum);
        sleep(1); // wait 1 second
    }

    return 0;
}