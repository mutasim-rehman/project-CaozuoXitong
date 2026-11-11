/* jobs.h - Part 1: parse the input file into a job table. */

#ifndef MINI_SCHED_JOBS_H
#define MINI_SCHED_JOBS_H

#include "common.h"

/* Read `path` into `t`. Exits with a clear error on any problem. */
void jobs_load(const char *path, job_table_t *t);

/* Reset per-run state (remaining, first_start, completion, flags). */
void jobs_reset(job_table_t *t);

/* Find a job by its string id. Returns index, or -1 if not found. */
int  jobs_find(const job_table_t *t, const char *id);

#endif
