/* shm.c - Part 6 of the Mini CPU Scheduler (scheduler side).
 *
 * POSIX shared memory (shm_open + ftruncate + mmap) carries an shm_report_t.
 * A named POSIX semaphore, initial value 0, tells the report program when
 * the data is complete. The report is started BEFORE the data is written,
 * so it really does block on the semaphore until the scheduler is done.
 */

#define _POSIX_C_SOURCE 200809L

#include "shm.h"
#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void report_publish(const char *report_path, const char *input_name,
                    const run_result_t *runs, int n_runs, int aging)
{
    /* Remove leftovers from a previous crashed run, if any. */
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_NAME);

    sem_t *ready = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0600, 0);
    if (ready == SEM_FAILED) die("sem_open(%s) failed: %s", SEM_NAME, strerror(errno));

    int fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) die("shm_open(%s) failed: %s", SHM_NAME, strerror(errno));
    if (ftruncate(fd, sizeof(shm_report_t)) != 0)
        die("ftruncate failed: %s", strerror(errno));

    shm_report_t *shm = mmap(NULL, sizeof(shm_report_t), PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) die("mmap failed: %s", strerror(errno));

    fflush(stdout);
    fflush(stderr);

    pid_t pid = fork();
    if (pid < 0) die("fork (report) failed: %s", strerror(errno));
    if (pid == 0) {
        close(fd);
        execl(report_path, "report", (char *)NULL);
        fprintf(stderr, "error: exec '%s' failed: %s\n", report_path, strerror(errno));
        _exit(127);
    }

    /* Parent: fill the segment, then signal that it is ready. */
    memset(shm, 0, sizeof(*shm));
    shm->n_runs = n_runs;
    shm->aging  = aging;
    snprintf(shm->input, sizeof(shm->input), "%s", input_name);
    for (int i = 0; i < n_runs && i < ALG_COUNT; ++i) shm->runs[i] = runs[i];

    sem_post(ready);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
        fprintf(stderr, "warning: report program exited with status %d\n",
                WEXITSTATUS(status));

    munmap(shm, sizeof(shm_report_t));
    close(fd);
    shm_unlink(SHM_NAME);
    sem_close(ready);
    sem_unlink(SEM_NAME);
}
