/* jobs.c - Part 1 of the Mini CPU Scheduler.
 *
 * Read and validate jobs.txt-style input. Lines look like:
 *   # comment
 *   P1  0  5
 *   P2  1  3  2      <-- optional 4th column: priority (bonus)
 */

#include "jobs.h"
#include "common.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------- small utilities --------- */

static int blank_or_comment(const char *s)
{
    while (*s && isspace((unsigned char)*s)) ++s;
    return *s == '\0' || *s == '#';
}

static int cmp_arrival_then_id(const void *a, const void *b)
{
    const job_t *x = (const job_t *)a;
    const job_t *y = (const job_t *)b;
    if (x->arrival != y->arrival) return x->arrival - y->arrival;
    return strcmp(x->id, y->id);
}

/* --------- jobs_load() --------- */

void jobs_load(const char *path, job_table_t *t)
{
    if (!path || !t) die("jobs_load: null argument");
    memset(t, 0, sizeof(*t));

    FILE *f = fopen(path, "r");
    if (!f) die("cannot open input file '%s'", path);

    char line[512];
    int lineno = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        if (blank_or_comment(line)) continue;

        if (t->n >= MAX_JOBS) {
            fclose(f);
            die("too many jobs (> %d) at line %d", MAX_JOBS, lineno);
        }

        job_t *j = &t->jobs[t->n];
        char id[64];
        int arrival, burst, priority = 0;

        int fields = sscanf(line, "%63s %d %d %d",
                            id, &arrival, &burst, &priority);
        if (fields < 3) {
            fclose(f);
            die("line %d: expected 'id arrival burst [priority]'", lineno);
        }
        if (strlen(id) >= MAX_ID_LEN) {
            fclose(f);
            die("line %d: job id '%s' is too long (max %d chars)",
                lineno, id, MAX_ID_LEN - 1);
        }
        if (arrival < 0) {
            fclose(f);
            die("line %d: arrival time must be >= 0", lineno);
        }
        if (burst <= 0) {
            fclose(f);
            die("line %d: burst time must be >= 1", lineno);
        }
        if (priority < 0) {
            fclose(f);
            die("line %d: priority must be >= 0", lineno);
        }

        /* duplicate id check */
        for (int i = 0; i < t->n; ++i) {
            if (strcmp(t->jobs[i].id, id) == 0) {
                fclose(f);
                die("line %d: duplicate job id '%s'", lineno, id);
            }
        }

        memset(j, 0, sizeof(*j));
        memcpy(j->id, id, strlen(id) + 1);
        j->arrival      = arrival;
        j->burst        = burst;
        j->priority     = priority;
        j->remaining    = burst;
        j->first_start  = -1;
        j->completion   = -1;
        j->eff_priority = priority;
        if (fields == 4) t->have_priority = 1;
        t->n++;
    }

    fclose(f);

    if (t->n == 0) die("input file '%s' has no jobs", path);

    /* Deterministic order: by arrival, then id. */
    qsort(t->jobs, t->n, sizeof(job_t), cmp_arrival_then_id);
}

/* --------- jobs_reset() --------- */

void jobs_reset(job_table_t *t)
{
    for (int i = 0; i < t->n; ++i) {
        job_t *j = &t->jobs[i];
        j->remaining    = j->burst;
        j->first_start  = -1;
        j->completion   = -1;
        j->wait_accum   = 0;
        j->eff_priority = j->priority;
        j->in_ready     = 0;
        j->finished     = 0;
    }
}

/* --------- jobs_find() --------- */

int jobs_find(const job_table_t *t, const char *id)
{
    for (int i = 0; i < t->n; ++i)
        if (strcmp(t->jobs[i].id, id) == 0) return i;
    return -1;
}
