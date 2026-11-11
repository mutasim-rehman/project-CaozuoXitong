/* shm.h - Part 6: hand final metrics to the report program. */

#ifndef MINI_SCHED_SHM_H
#define MINI_SCHED_SHM_H

#include "common.h"

/* Create the shared memory segment and "ready" semaphore, fork+exec the
   report program at `report_path`, write the results, post the semaphore,
   wait for the report to exit, then remove the segment and semaphore. */
void report_publish(const char *report_path, const char *input_name,
                    const run_result_t *runs, int n_runs, int aging);

#endif
