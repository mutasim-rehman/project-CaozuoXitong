/* common.h - shared types, constants and helpers for the Mini CPU Scheduler.
 *
 * Operating Systems Lab - Mini CPU Scheduler
 */

#ifndef MINI_SCHED_COMMON_H
#define MINI_SCHED_COMMON_H

#include <stddef.h>

/* ---------- limits and conventions ---------- */

#define MAX_JOBS      64
#define MAX_ID_LEN    16
#define MAX_GANTT     8192
#ifndef TIME_UNIT_MS
#define TIME_UNIT_MS  200    /* one "time unit" of simulated CPU work */
#endif

/* POSIX IPC names used between the scheduler and the report program. */
#define SHM_NAME   "/mini_sched_shm"
#define SEM_NAME   "/mini_sched_ready"

/* ---------- algorithm identifiers ---------- */

typedef enum {
    ALG_FCFS = 0,
    ALG_SJF  = 1,
    ALG_SRTF = 2,
    ALG_RR   = 3,
    ALG_PRIO = 4,       /* bonus: preemptive priority (optionally with aging) */
    ALG_COUNT
} algo_t;

/* Human-readable algorithm names (index by algo_t). */
extern const char *ALGO_NAMES[ALG_COUNT];

/* ---------- job table ---------- */

typedef struct {
    char id[MAX_ID_LEN];     /* job id as given in the input file           */
    int  arrival;            /* arrival time, >= 0                          */
    int  burst;              /* burst (total CPU requirement), >= 1         */
    int  priority;           /* smaller = higher priority; 0 if unspecified */

    /* mutable per-run state (reset between runs in "all" mode) */
    int  remaining;          /* remaining CPU time, starts at burst         */
    int  first_start;        /* -1 until the first CPU allocation           */
    int  completion;         /* -1 until the job finishes                   */
    int  wait_accum;         /* units waited since last on CPU (aging)      */
    int  eff_priority;       /* priority after aging                        */
    int  in_ready;           /* flag: job currently sitting in ready queue  */
    int  finished;           /* flag: remaining == 0 and reported           */
} job_t;

typedef struct {
    job_t jobs[MAX_JOBS];
    int   n;                 /* number of jobs                              */
    int   have_priority;     /* 1 if any input line had an explicit prio    */
} job_table_t;

/* ---------- Gantt chart entry (one per unit, then merged for printing) ---------- */

typedef struct {
    int t_start;             /* inclusive                                   */
    int t_end;               /* exclusive                                   */
    char id[MAX_ID_LEN];     /* job id, "IDLE", or "CS"                     */
} gantt_seg_t;

/* ---------- run results (used for comparison table and shared-memory report) ---------- */

typedef struct {
    char id[MAX_ID_LEN];
    int  arrival;
    int  burst;
    int  priority;
    int  completion;
    int  turnaround;
    int  waiting;
    int  response;
} job_metrics_t;

typedef struct {
    algo_t algo;
    int    quantum;              /* only meaningful for RR                  */
    int    cs_cost;              /* bonus: idle units per context switch    */
    int    n;
    job_metrics_t m[MAX_JOBS];
    double avg_waiting;
    double avg_turnaround;
    double avg_response;
    int    context_switches;
    int    total_time;           /* final simulated time                    */
    int    valid;                /* 1 if the run actually ran               */
} run_result_t;

/* Shared memory layout for Part 6. */
typedef struct {
    int           n_runs;
    int           aging;         /* bonus option, for labels only           */
    char          input[128];    /* input file name, for the report header  */
    run_result_t  runs[ALG_COUNT];
} shm_report_t;

/* ---------- small helpers ---------- */

void die(const char *fmt, ...);
int  algo_from_string(const char *s, algo_t *out);

#endif /* MINI_SCHED_COMMON_H */
