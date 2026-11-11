/* output.h - Part 4: Gantt chart, per-job table, averages, comparison. */

#ifndef MINI_SCHED_OUTPUT_H
#define MINI_SCHED_OUTPUT_H

#include <stddef.h>
#include "common.h"

/* "FCFS", "Round Robin (q = 2)", "Priority (aging 3)", ... */
void algo_label(const run_result_t *r, int aging, char *buf, size_t len);

void output_gantt(const gantt_seg_t *segs, int n_segs);
void output_job_table(const run_result_t *r);
void output_comparison(const run_result_t *runs, int n_runs, int aging);

#endif
