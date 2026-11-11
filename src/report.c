/* report.c - Part 6 of the Mini CPU Scheduler (report program).
 *
 * Started by the scheduler with fork() + exec(). Waits on the named
 * semaphore until the scheduler has finished writing, then attaches to the
 * shared memory segment and prints the final report. The scheduler owns the
 * segment and semaphore and removes them after this program exits.
 */

#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "output.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    sem_t *ready = sem_open(SEM_NAME, 0);
    if (ready == SEM_FAILED) die("report: sem_open(%s): %s", SEM_NAME, strerror(errno));

    while (sem_wait(ready) != 0) {
        if (errno != EINTR) die("report: sem_wait: %s", strerror(errno));
    }

    int fd = shm_open(SHM_NAME, O_RDONLY, 0);
    if (fd < 0) die("report: shm_open(%s): %s", SHM_NAME, strerror(errno));

    const shm_report_t *shm = mmap(NULL, sizeof(shm_report_t), PROT_READ,
                                   MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) die("report: mmap: %s", strerror(errno));

    printf("\n==================== FINAL REPORT (report process, pid %d) ====================\n",
           (int)getpid());
    printf("Input: %s\n", shm->input);

    for (int i = 0; i < shm->n_runs && i < ALG_COUNT; ++i) {
        const run_result_t *r = &shm->runs[i];
        if (!r->valid) continue;
        char label[64];
        algo_label(r, shm->aging, label, sizeof(label));
        printf("\n--- %s ---", label);
        output_job_table(r);
    }

    if (shm->n_runs > 1) {
        printf("\nComparison:");
        output_comparison(shm->runs, shm->n_runs, shm->aging);
    }
    printf("================================================================================\n");
    fflush(stdout);

    munmap((void *)shm, sizeof(shm_report_t));
    close(fd);
    sem_close(ready);
    return 0;
}
