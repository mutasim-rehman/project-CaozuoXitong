/* sched.c - Parts 2 and 3 of the Mini CPU Scheduler.
 *
 * - ready queue (array-backed FIFO, scanned for non-RR algorithms)
 * - FCFS, SJF, SRTF, Round Robin and (bonus) preemptive priority with aging
 * - one POSIX thread per job + one arrival thread
 * - dispatcher advances simulated time one unit at a time and uses
 *   semaphores to hand the CPU to a single job thread at a time.
 *
 * Synchronisation summary (also listed in the README):
 *   queue_mtx     mutex -- protects: ready queue, every job_t field that
 *                                    changes at runtime, counters, Gantt log.
 *   print_mtx     mutex -- serialises stdout output so lines do not interleave.
 *   run[i]        unnamed sem, initial 0 -- dispatcher posts, job i wakes.
 *   done          unnamed sem, initial 0 -- job posts after one unit, dispatcher wakes.
 *   tick          unnamed sem, initial 0 -- dispatcher posts, arrival thread wakes.
 *   arrivals_done unnamed sem, initial 0 -- arrival thread posts, dispatcher wakes.
 *
 * g_t (simulated clock) and g_stop are written only by the dispatcher. Other
 * threads read them only after a sem_wait() on a semaphore the dispatcher
 * posted, while the dispatcher itself is blocked, so no read overlaps a write.
 */

#define _POSIX_C_SOURCE 200809L

#include "sched.h"
#include "common.h"
#include "jobs.h"
#include "logger.h"

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Simulate one time unit of work (sleep is only used to simulate work). */
static void sleep_unit(void)
{
    struct timespec ts = {
        .tv_sec  = TIME_UNIT_MS / 1000,
        .tv_nsec = (long)(TIME_UNIT_MS % 1000) * 1000000L
    };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
}

/* ========================================================================
 * Module-global state for the currently running simulation.
 * Only one sched_run() runs at a time, so globals are fine and simpler
 * than threading a context pointer through every signal handler.
 * ======================================================================== */

static job_table_t     *g_tab;
static sched_opts_t     g_opts;
static sched_output_t  *g_out;

/* Ready queue (indices into g_tab->jobs). Circular buffer. */
static int   g_ready[MAX_JOBS];
static int   g_r_head;
static int   g_r_tail;
static int   g_r_size;

/* Simulated time, one unit per loop. */
static int   g_t;

/* Per-unit "who is running" trace, for merging into Gantt segments. */
static char  g_trace[MAX_GANTT][MAX_ID_LEN];
static int   g_trace_n;

/* How many units job i has run under its current quantum (RR). */
static int   g_q_used;

/* Whether this unit must be charged as a CS (used to insert CS idle units). */
static int   g_pending_cs;

/* "Stop" flag for all threads at end-of-run. */
static int   g_stop;

/* Threads */
static pthread_t   g_arrival_th;
static pthread_t   g_job_th[MAX_JOBS];

/* Synchronisation */
static pthread_mutex_t g_queue_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_print_mtx = PTHREAD_MUTEX_INITIALIZER;
static sem_t  g_run[MAX_JOBS];
static sem_t  g_done;
static sem_t  g_tick;
static sem_t  g_arrivals_done;

/* ========================================================================
 * Ready queue helpers (caller must hold g_queue_mtx)
 * ======================================================================== */

static void rq_init(void)        { g_r_head = g_r_tail = g_r_size = 0; }
static int  rq_empty(void)       { return g_r_size == 0; }
static int  rq_count(void)       { return g_r_size; }

static void rq_push_tail(int idx)
{
    g_ready[g_r_tail] = idx;
    g_r_tail = (g_r_tail + 1) % MAX_JOBS;
    g_r_size++;
    g_tab->jobs[idx].in_ready = 1;
}

/* Remove the ready-queue entry at logical position `pos` (0..size-1). */
static void rq_remove_at(int pos)
{
    int cur = (g_r_head + pos) % MAX_JOBS;
    int removed = g_ready[cur];
    int next;
    for (int i = pos; i < g_r_size - 1; ++i) {
        next = (cur + 1) % MAX_JOBS;
        g_ready[cur] = g_ready[next];
        cur = next;
    }
    g_r_tail = (g_r_tail - 1 + MAX_JOBS) % MAX_JOBS;
    g_r_size--;
    g_tab->jobs[removed].in_ready = 0;
}

static int rq_at(int pos) { return g_ready[(g_r_head + pos) % MAX_JOBS]; }

/* ========================================================================
 * Algorithm selection (caller holds g_queue_mtx)
 *
 * Returns the logical position in the ready queue to dispatch next,
 * or -1 if the queue is empty.
 * The queue entry is NOT popped here; the dispatcher pops it after
 * handling preemption/first-dispatch bookkeeping.
 *
 * running_idx is -1 if no job is currently holding the CPU, otherwise the
 * job index that ran on the previous unit (relevant to SRTF / Priority).
 * ======================================================================== */

static int alg_pick(algo_t a, int running_idx)
{
    if (rq_empty()) return -1;

    switch (a) {

    case ALG_FCFS:
        /* Jobs are enqueued in arrival order, so head is correct. */
        return 0;

    case ALG_SJF: {
        /* Shortest burst, ties by arrival then id. */
        int best = 0;
        for (int p = 1; p < g_r_size; ++p) {
            job_t *bj = &g_tab->jobs[rq_at(best)];
            job_t *cj = &g_tab->jobs[rq_at(p)];
            if (cj->burst <  bj->burst) best = p;
            else if (cj->burst == bj->burst) {
                if (cj->arrival <  bj->arrival) best = p;
                else if (cj->arrival == bj->arrival &&
                         strcmp(cj->id, bj->id) < 0) best = p;
            }
        }
        return best;
    }

    case ALG_SRTF: {
        /* Shortest remaining, ties by arrival then id; if the running
           job ties with a candidate, the running job keeps the CPU
           (handled by the caller by preferring running_idx). */
        int best = 0;
        for (int p = 1; p < g_r_size; ++p) {
            job_t *bj = &g_tab->jobs[rq_at(best)];
            job_t *cj = &g_tab->jobs[rq_at(p)];
            if (cj->remaining <  bj->remaining) best = p;
            else if (cj->remaining == bj->remaining) {
                if (cj->arrival <  bj->arrival) best = p;
                else if (cj->arrival == bj->arrival &&
                         strcmp(cj->id, bj->id) < 0) best = p;
            }
        }
        (void)running_idx;
        return best;
    }

    case ALG_RR:
        /* Pure FIFO, same as FCFS. */
        return 0;

    case ALG_PRIO: {
        /* Smaller priority value = higher priority. Ties: arrival, id. */
        int best = 0;
        for (int p = 1; p < g_r_size; ++p) {
            job_t *bj = &g_tab->jobs[rq_at(best)];
            job_t *cj = &g_tab->jobs[rq_at(p)];
            if (cj->eff_priority <  bj->eff_priority) best = p;
            else if (cj->eff_priority == bj->eff_priority) {
                if (cj->arrival <  bj->arrival) best = p;
                else if (cj->arrival == bj->arrival &&
                         strcmp(cj->id, bj->id) < 0) best = p;
            }
        }
        (void)running_idx;
        return best;
    }

    default:
        return 0;
    }
}

/* Non-preemptive algorithms (FCFS, SJF) only re-pick when the CPU is free. */
static int alg_is_preemptive(algo_t a)
{
    return a == ALG_SRTF || a == ALG_RR || a == ALG_PRIO;
}

/* ========================================================================
 * Job thread: wait for the dispatcher, simulate one time unit, signal done.
 * ======================================================================== */

static void *job_thread(void *arg)
{
    int idx = (int)(intptr_t)arg;
    job_t *j = &g_tab->jobs[idx];

    for (;;) {
        sem_wait(&g_run[idx]);

        if (g_stop) {
            /* Woken only to exit. */
            sem_post(&g_done);
            return NULL;
        }

        /* Simulate one unit of CPU work and print the live line. */
        pthread_mutex_lock(&g_print_mtx);
        if (g_opts.verbose) {
            printf("[t=%3d] %s running   (remaining %d)\n",
                   g_t, j->id, j->remaining);
            fflush(stdout);
        }
        pthread_mutex_unlock(&g_print_mtx);

        sleep_unit();

        sem_post(&g_done);
    }
}

/* ========================================================================
 * Arrival thread: on each `tick`, enqueue every job whose arrival <= g_t.
 * ======================================================================== */

static void *arrival_thread(void *arg)
{
    (void)arg;
    for (;;) {
        sem_wait(&g_tick);
        if (g_stop) { sem_post(&g_arrivals_done); return NULL; }

        /* Enqueue any job whose arrival time has come due, exactly once.
           A job is "new" if it has never been dispatched: remaining ==
           burst AND first_start == -1. That is the only time the
           arrival thread is allowed to enqueue it. */
        pthread_mutex_lock(&g_queue_mtx);
        for (int i = 0; i < g_tab->n; ++i) {
            job_t *j = &g_tab->jobs[i];
            if (j->arrival <= g_t
                && !j->finished
                && !j->in_ready
                && j->remaining == j->burst
                && j->first_start == -1) {
                rq_push_tail(i);
                log_event(j->arrival, LOG_ARRIVED, j->id);
            }
        }
        pthread_mutex_unlock(&g_queue_mtx);

        sem_post(&g_arrivals_done);
    }
}

/* ========================================================================
 * Gantt trace helpers (caller holds g_queue_mtx)
 * ======================================================================== */

static void trace_push(const char *id)
{
    if (g_trace_n >= MAX_GANTT) return;
    snprintf(g_trace[g_trace_n], MAX_ID_LEN, "%s", id);
    g_trace_n++;
}

/* Merge the per-unit trace into contiguous segments stored in g_out->segs. */
static void trace_merge(void)
{
    g_out->n_segs = 0;
    int i = 0;
    while (i < g_trace_n) {
        int j = i + 1;
        while (j < g_trace_n && strcmp(g_trace[j], g_trace[i]) == 0) j++;
        if (g_out->n_segs < MAX_GANTT) {
            gantt_seg_t *s = &g_out->segs[g_out->n_segs++];
            s->t_start = i;
            s->t_end   = j;
            snprintf(s->id, MAX_ID_LEN, "%s", g_trace[i]);
        }
        i = j;
    }
    g_out->raw_units = g_trace_n;
}

/* ========================================================================
 * Setup / teardown
 * ======================================================================== */

static void sem_init_or_die(sem_t *s, unsigned v)
{
    if (sem_init(s, 0, v) != 0) die("sem_init failed: %s", strerror(errno));
}

static void sched_setup(void)
{
    rq_init();
    g_t = 0;
    g_trace_n = 0;
    g_q_used = 0;
    g_pending_cs = 0;
    g_stop = 0;

    jobs_reset(g_tab);

    sem_init_or_die(&g_done, 0);
    sem_init_or_die(&g_tick, 0);
    sem_init_or_die(&g_arrivals_done, 0);
    for (int i = 0; i < g_tab->n; ++i) sem_init_or_die(&g_run[i], 0);

    /* Threads */
    for (int i = 0; i < g_tab->n; ++i) {
        if (pthread_create(&g_job_th[i], NULL, job_thread,
                           (void *)(intptr_t)i) != 0)
            die("pthread_create(job %d) failed", i);
    }
    if (pthread_create(&g_arrival_th, NULL, arrival_thread, NULL) != 0)
        die("pthread_create(arrival) failed");
}

static void sched_teardown(void)
{
    /* Wake every thread so it can observe g_stop and exit cleanly. */
    g_stop = 1;
    for (int i = 0; i < g_tab->n; ++i) sem_post(&g_run[i]);
    sem_post(&g_tick);

    for (int i = 0; i < g_tab->n; ++i) pthread_join(g_job_th[i], NULL);
    pthread_join(g_arrival_th, NULL);

    for (int i = 0; i < g_tab->n; ++i) sem_destroy(&g_run[i]);
    sem_destroy(&g_done);
    sem_destroy(&g_tick);
    sem_destroy(&g_arrivals_done);
}

/* ========================================================================
 * Dispatcher (main simulation loop)
 * ======================================================================== */

/* Returns 1 if any job is still unfinished. */
static int any_unfinished(void)
{
    for (int i = 0; i < g_tab->n; ++i)
        if (!g_tab->jobs[i].finished) return 1;
    return 0;
}

/* Add 1 to wait_accum for every ready-but-not-running job. */
static void tick_waiters(int running_idx)
{
    for (int p = 0; p < g_r_size; ++p) {
        int idx = rq_at(p);
        if (idx != running_idx) g_tab->jobs[idx].wait_accum++;
    }
    /* Also age priorities if the aging option is on. */
    if (g_opts.aging > 0 && g_tab->have_priority) {
        for (int p = 0; p < g_r_size; ++p) {
            int idx = rq_at(p);
            job_t *j = &g_tab->jobs[idx];
            if (j->wait_accum > 0
                && j->wait_accum % g_opts.aging == 0
                && j->eff_priority > 0)
                j->eff_priority--;
        }
    }
}

void sched_run(job_table_t *t, const sched_opts_t *opts, sched_output_t *out)
{
    if (!t || !opts || !out) die("sched_run: null arg");

    g_tab = t;
    g_opts = *opts;
    g_out = out;
    memset(out, 0, sizeof(*out));

    if (g_opts.algo == ALG_RR && g_opts.quantum <= 0)
        die("Round Robin requires a positive quantum");

    sched_setup();

    int running_idx = -1;   /* job holding the CPU right now, or -1 */
    int prev_unit_job = -1; /* who ran on the previous unit, or -1 (IDLE) */

    while (any_unfinished()) {

        /* 1. Let the arrival thread enqueue any jobs whose arrival <= g_t. */
        sem_post(&g_tick);
        sem_wait(&g_arrivals_done);

        pthread_mutex_lock(&g_queue_mtx);

        /* 2. Decide who should run this unit. */

        /* 2a. Preempt the running job if the algorithm says so. */
        if (running_idx != -1) {
            job_t *rj = &g_tab->jobs[running_idx];

            if (g_opts.algo == ALG_RR) {
                if (g_q_used >= g_opts.quantum && rq_count() > 0) {
                    /* Quantum expired AND someone is waiting -> preempt. */
                    log_event(g_t, LOG_PREEMPTED, rj->id);
                    rq_push_tail(running_idx);
                    running_idx = -1;
                    g_q_used = 0;
                }
            } else if (alg_is_preemptive(g_opts.algo)) {
                /* SRTF / PRIO: compare running vs best in ready queue. */
                if (!rq_empty()) {
                    /* Temporarily put running job back in queue to let
                       alg_pick consider it uniformly, then re-decide. */
                    rq_push_tail(running_idx);
                    int pos = alg_pick(g_opts.algo, running_idx);
                    int chosen = rq_at(pos);

                    /* Tie-break: a running job keeps the CPU when a
                       candidate ties its key. SRTF: same remaining.
                       PRIO: same effective priority. */
                    int tie = 0;
                    if (chosen != running_idx) {
                        if (g_opts.algo == ALG_SRTF) {
                            tie = (g_tab->jobs[chosen].remaining
                                == g_tab->jobs[running_idx].remaining);
                        } else if (g_opts.algo == ALG_PRIO) {
                            tie = (g_tab->jobs[chosen].eff_priority
                                == g_tab->jobs[running_idx].eff_priority);
                        }
                        if (tie) {
                            chosen = running_idx;
                            for (int p = 0; p < g_r_size; ++p)
                                if (rq_at(p) == running_idx) { pos = p; break; }
                        }
                    }

                    if (chosen != running_idx) {
                        rq_remove_at(pos);
                        /* running_idx stays in the queue (already pushed) */
                        log_event(g_t, LOG_PREEMPTED, rj->id);
                        running_idx = chosen;
                        g_q_used = 0;
                    } else {
                        /* Running job wins -> pop it back out, keep running. */
                        for (int p = 0; p < g_r_size; ++p) {
                            if (rq_at(p) == running_idx) {
                                rq_remove_at(p);
                                break;
                            }
                        }
                    }
                }
            }
            /* FCFS / SJF: no preemption while running_idx is set. */
        }

        /* 2b. Pick a new job if the CPU is free. */
        if (running_idx == -1 && !rq_empty()) {
            int pos = alg_pick(g_opts.algo, -1);
            running_idx = rq_at(pos);
            rq_remove_at(pos);
            g_q_used = 0;
        }

        int unit_job = running_idx;   /* -1 means IDLE this unit */

        /* 2c. Context switch: CPU goes from one job directly to a
               different job. First dispatch and IDLE transitions do
               not count. */
        if (unit_job != -1 && prev_unit_job != -1 && prev_unit_job != unit_job) {
            g_out->context_switches++;
            if (g_opts.verbose) {
                pthread_mutex_lock(&g_print_mtx);
                printf("[t=%3d] ---- context switch: %s -> %s ----\n",
                       g_t,
                       g_tab->jobs[prev_unit_job].id,
                       g_tab->jobs[unit_job].id);
                fflush(stdout);
                pthread_mutex_unlock(&g_print_mtx);
            }
            /* Bonus: charge cs_cost units of overhead before the job runs. */
            for (int k = 0; k < g_opts.cs_cost; ++k) {
                if (g_opts.verbose) {
                    pthread_mutex_lock(&g_print_mtx);
                    printf("[t=%3d] (context-switch overhead)\n", g_t);
                    fflush(stdout);
                    pthread_mutex_unlock(&g_print_mtx);
                }
                trace_push("CS");
                tick_waiters(unit_job);
                pthread_mutex_unlock(&g_queue_mtx);
                sleep_unit();
                pthread_mutex_lock(&g_queue_mtx);
                g_t++;
            }
        }

        /* 2d. Log start / resume. */
        if (unit_job != -1) {
            job_t *j = &g_tab->jobs[unit_job];
            /* Aging boost lasts only until the job gets the CPU. */
            j->eff_priority = j->priority;
            j->wait_accum   = 0;
            if (j->first_start == -1) {
                j->first_start = g_t;
                log_event(g_t, LOG_STARTED, j->id);
            } else if (prev_unit_job != unit_job) {
                log_event(g_t, LOG_RESUMED, j->id);
            }
        }

        /* 3. Run one unit (or IDLE if nothing to run). */
        if (unit_job == -1) {
            if (g_opts.verbose) {
                pthread_mutex_lock(&g_print_mtx);
                printf("[t=%3d] IDLE\n", g_t);
                fflush(stdout);
                pthread_mutex_unlock(&g_print_mtx);
            }
            trace_push("IDLE");
            g_t++;
            prev_unit_job = -1;
            pthread_mutex_unlock(&g_queue_mtx);
            sleep_unit();
            continue;
        }

        trace_push(g_tab->jobs[unit_job].id);
        tick_waiters(unit_job);
        pthread_mutex_unlock(&g_queue_mtx);

        /* Hand the CPU to the job thread for one unit and block until it
           reports the unit is over. While the job runs, the dispatcher
           and arrival thread are both blocked, so the job may read g_t and
           its own remaining time safely (semaphore protocol). */
        sem_post(&g_run[unit_job]);
        sem_wait(&g_done);

        /* Figure 2 step 5: dispatcher updates remaining time and metrics. */
        pthread_mutex_lock(&g_queue_mtx);
        job_t *j = &g_tab->jobs[unit_job];
        j->remaining--;
        g_q_used++;
        g_t++;
        prev_unit_job = unit_job;
        if (j->remaining == 0) {
            j->finished   = 1;
            j->completion = g_t;
            log_event(g_t, LOG_FINISHED, j->id);
            running_idx = -1;
            g_q_used = 0;
        }
        pthread_mutex_unlock(&g_queue_mtx);
    }

    pthread_mutex_lock(&g_queue_mtx);
    trace_merge();
    pthread_mutex_unlock(&g_queue_mtx);

    /* Fill the summary result structure. */
    run_result_t *r = &g_out->result;
    r->algo = g_opts.algo;
    r->quantum = g_opts.quantum;
    r->cs_cost = g_opts.cs_cost;
    r->n = g_tab->n;
    r->total_time = g_t;
    r->context_switches = g_out->context_switches;
    r->valid = 1;

    double sum_wait = 0, sum_tat = 0, sum_rt = 0;
    for (int i = 0; i < g_tab->n; ++i) {
        job_t *jj = &g_tab->jobs[i];
        job_metrics_t *m = &r->m[i];
        snprintf(m->id, MAX_ID_LEN, "%s", jj->id);
        m->arrival    = jj->arrival;
        m->burst      = jj->burst;
        m->priority   = jj->priority;
        m->completion = jj->completion;
        m->turnaround = jj->completion - jj->arrival;
        m->waiting    = m->turnaround - jj->burst;
        m->response   = jj->first_start - jj->arrival;
        sum_wait += m->waiting;
        sum_tat  += m->turnaround;
        sum_rt   += m->response;
    }
    r->avg_waiting    = sum_wait / g_tab->n;
    r->avg_turnaround = sum_tat  / g_tab->n;
    r->avg_response   = sum_rt   / g_tab->n;

    sched_teardown();
}
