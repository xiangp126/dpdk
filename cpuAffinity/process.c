#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h> /* sysconf */
#include <stdlib.h> /* exit */
#include <stdio.h>

int main(int argc, char *argv[])
{
    int i, ncpus;
    cpu_set_t mask;
    unsigned long bitmask = 0;

    /* mask = {__bits = {0 <repeats 16 times>}} */
    CPU_ZERO(&mask);

    /*
     * add cpu0 to cpu set
     * mask = {__bits = {1, 0 <repeats 15 times>}}
     * 0x0000 ... 0001, 0 <repeats 15 times>
     */
    CPU_SET(0, &mask);

    /*
     * add cpu2 to cpu set
     * mask = {__bits = {5, 0 <repeats 15 times>}}
     * 0x0000 ... 0101, 0 <repeats 15 times>
     */
    CPU_SET(2, &mask);

    /* set the cpu affinity for current pid */
    pid_t pid = getpid();
    if (sched_setaffinity(pid, sizeof(cpu_set_t), &mask) == -1) {
        perror("sched_setaffinity error");
        exit(EXIT_FAILURE);
    }

    CPU_ZERO(&mask);

     /* get the cpu affinity for a pid */
    if (sched_getaffinity(0, sizeof(cpu_set_t), &mask) == -1) {
        perror("sched_getaffinity error");
        exit(EXIT_FAILURE);
    }

    /* get logical cpu number */
    ncpus = sysconf(_SC_NPROCESSORS_CONF);

    bitmask = 0;
    for (int cid = 0; cid < ncpus; ++cid) {
        if (CPU_ISSET(cid, &mask)) {
            bitmask |= 0x1 << cid;
            printf("Processor #%d is set\n", cid);
        }
    }

    printf("Total CPU numbers are: %d\n", ncpus);
    printf("CPU bit mask = %#lx\n", bitmask);
    printf("use htop to check which process was used\n");

    while (1) {
        //
    }

    exit(EXIT_SUCCESS);
}
