#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]) {
    int pid = fork();

    if (pid == 0)
    {
        exec(argv[1], argv + 1);
    }
    else
    {
        int *wtime = (int *)malloc(sizeof(int)),
            *rtime = (int *)malloc(sizeof(int));
        waitx(wtime, rtime);
        printf(1, "Runtime: %d,\nWtime: %d\n\n", *rtime, *wtime);
    }

    exit();
}
