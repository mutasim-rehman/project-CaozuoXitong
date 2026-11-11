/* sched.h - Parts 2 and 3: scheduling algorithms + live thread execution. */

#ifndef MINI_SCHED_SCHED_H
#define MINI_SCHED_SCHED_H

#include "common.h"

/* Options passed by main() for a single run. */
typedef struct {
    algo_t algo;
    int    quantum;      /* required for RR, else 0           */
    int    cs_cost;      /* bonus: idle units per ctx switch  */
    int    aging;        /* bonus: units of wait per +1 prio  */
    int    verbose;      /* 1 = print per-unit lines          */
} sched_opts_t;

/* Collected per-run outputs. */
typedef struct {
    gantt_seg_t segs[MAX_GANTT];
    int         n_segs;             /* after merging */
    int         raw_units;          /* total simulated units (incl. IDLE/CS) */
    int         context_switches;
    run_result_t result;            /* summary metrics                       */
} sched_output_t;

/* Run one algorithm on the given job table. Fills `out`. */
void sched_run(job_table_t *t, const sched_opts_t *opts, sched_output_t *out);

#endif
