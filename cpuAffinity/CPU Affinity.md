## CPU Affinity

<https://blog.csdn.net/STN_LCD/article/details/78193963>

### Contents
- [concept of bit mask](#concept)
- [__cpu\_mask](#cpumask)
- [cpu\_set\_t](#cpusett)
- [main point of `sched.h`](#schedh)
    - [__USE\_GNU](#usegnu)
    - [\_GNU\_SOURCE](#gnusource)
- [CPU_SET](#cpuset)
- [Demo Source](#demo)
- [Makefile Command](#make)

<a id=concept></a>
### concept of bit mask
```c
0x00000001
    is processor #0

0x00000003
    is processors #0 and #1

0xFFFFFFFF
    is all processors (#0 through #31)
```

<a id=cpumask></a>
#### __cpu\_mask
```c
#define __CPU_SETSIZE	1024
#define __NCPUBITS	(8 * sizeof (__cpu_mask))

/* Type for array elements in 'cpu_set_t'.  */
typedef unsigned long int __cpu_mask;
```

<a id=cpusett></a>
### cpu\_set\_t
```c
typedef struct
{
  __cpu_mask __bits[__CPU_SETSIZE / __NCPUBITS];
} cpu_set_t;
```

so

```c
(gdb) p cpumask
$20 = {
  __bits = {15, 0 <repeats 15 times>}
}

(gdb) p sizeof(cpumask .__bits [0])
$18 = 8

(gdb) p sizeof(cpumask)
$17 = 128
// 128 B = 128 * 8 bit = 1024 bit = __CPU_SETSIZE bit
```

---
<a id=schedh></a>
### sched.h
#### path
/include/sched.h

<a id=usegnu></a>
#### __USE\_GNU
```c
#ifndef	_SCHED_H
#define	_SCHED_H	1

#include <features.h>

/* Get type definitions.  */
#include <bits/types.h>
...
```

need define macro `__USE_GNU`

```c
...
#ifdef __USE_GNU
/* Access macros for `cpu_set'.  */
# define CPU_SETSIZE __CPU_SETSIZE
# define CPU_SET(cpu, cpusetp)	 __CPU_SET_S (cpu, sizeof (cpu_set_t), cpusetp)
# define CPU_CLR(cpu, cpusetp)	 __CPU_CLR_S (cpu, sizeof (cpu_set_t), cpusetp)
# define CPU_ISSET(cpu, cpusetp) __CPU_ISSET_S (cpu, sizeof (cpu_set_t), \
						cpusetp)
# define CPU_ZERO(cpusetp)	 __CPU_ZERO_S (sizeof (cpu_set_t), cpusetp)
# define CPU_COUNT(cpusetp)	 __CPU_COUNT_S (sizeof (cpu_set_t), cpusetp)

...

/* Set the CPU affinity for a task */
extern int sched_setaffinity (__pid_t __pid, size_t __cpusetsize,
			      const cpu_set_t *__cpuset) __THROW;

/* Get the CPU affinity for a task */
extern int sched_getaffinity (__pid_t __pid, size_t __cpusetsize,
			      cpu_set_t *__cpuset) __THROW;
#endif

__END_DECLS

#endif /* sched.h */
```

<a id=gnusource></a>
#### _GNU\_SOURCE
notice `feature.h` was included, look into it `feature.h` there was

```c
#ifdef	_GNU_SOURCE
# define __USE_GNU	1
#endif
```

so

```c
#define _GNU_SOURCE
```
at top of `main.c`

<a id=cpuset></a>
### CPU_SET
```c
# define CPU_SET(cpu, cpusetp)	 __CPU_SET_S (cpu, sizeof (cpu_set_t), cpusetp)
-->
#define __CPU_SET_S(cpu, setsize, cpusetp) \
  (__extension__							      \
   ({ size_t __cpu = (cpu);						      \
      __cpu / 8 < (setsize)						      \
      ? (((__cpu_mask *) ((cpusetp)->__bits))[__CPUELT (__cpu)]		      \
	 |= __CPUMASK (__cpu))						      \
      : 0; }))
```

### Demo Source
#### process affinity
```c
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
```

#### thread affinity
```c


```