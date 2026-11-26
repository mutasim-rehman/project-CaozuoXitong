// report.cpp - Part 6: Separate report process started via fork() + exec().

#define _DEFAULT_SOURCE
#include "common.h"

#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <unistd.h>

int main() {
    sem_t* ready = sem_open(SEM_NAME, 0);
    if (ready == SEM_FAILED) { cerr << "report: sem_open failed\n"; return 1; }

    // Wait until the scheduler finishes writing shared memory.
    sem_wait(ready);

    int fd = shm_open(SHM_NAME, O_RDONLY, 0);
    if (fd < 0) { cerr << "report: shm_open failed\n"; return 1; }

    SharedReport* shm = (SharedReport*)mmap(
        NULL, sizeof(SharedReport), PROT_READ, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { cerr << "report: mmap failed\n"; return 1; }

    cout << "\n==================== FINAL REPORT (report process, pid "
         << getpid() << ") ====================\n";
    cout << "Input: " << shm->input_file << "\n";

    for (int i = 0; i < shm->n_runs; ++i) {
        cout << "\n--- " << shm->runs[i].algo_name << " ---";
        print_job_table(shm->runs[i]);
    }

    if (shm->n_runs > 1) {
        cout << "\nComparison:";
        print_comparison(shm->runs, shm->n_runs);
    }
    cout << "================================================================================\n";

    munmap(shm, sizeof(SharedReport));
    close(fd);
    sem_close(ready);
    return 0;
}
