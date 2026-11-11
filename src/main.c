/* main.c - Mini CPU Scheduler entry point.
 *
 *   ./scheduler <input_file> <algorithm> [quantum] [--cs N] [--aging N] [--quiet]
 *   algorithm : fcfs | sjf | srtf | rr | prio | all
 */

#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "jobs.h"
#include "logger.h"
#include "output.h"
#include "sched.h"
#include "shm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_FILE "schedule.log"

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s <input_file> <algorithm> [quantum] [--cs N] [--aging N] [--quiet]\n"
        "  algorithm : fcfs | sjf | srtf | rr | prio | all\n"
        "  quantum   : required for rr and all\n"
        "  --cs N    : bonus, N time units of overhead per context switch\n"
        "  --aging N : bonus (prio), raise priority by 1 per N units waited\n"
        "  --quiet   : do not print the per-unit execution lines\n"
        "examples:\n"
        "  %s inputs/jobs1.txt fcfs\n"
        "  %s inputs/jobs1.txt rr 2\n"
        "  %s inputs/jobs1.txt all 2\n",
        prog, prog, prog, prog);
    exit(1);
}

static int parse_nonneg(const char *s, int *out)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || v < 0 || v > 1000000) return 0;
    *out = (int)v;
    return 1;
}

/* Path of the report binary: same directory as this executable. */
static void report_path_from(const char *argv0, char *buf, size_t len)
{
    const char *slash = strrchr(argv0, '/');
    if (slash)
        snprintf(buf, len, "%.*s/report", (int)(slash - argv0), argv0);
    else
        snprintf(buf, len, "./report");
}

/* Large; keep out of the stack. */
static sched_output_t g_run_out;

int main(int argc, char **argv)
{
    if (argc < 3) usage(argv[0]);

    const char *input = argv[1];
    const char *alg_s = argv[2];
    int all_mode = (strcmp(alg_s, "all") == 0);
    algo_t single = ALG_FCFS;
    if (!all_mode && !algo_from_string(alg_s, &single)) {
        fprintf(stderr, "error: unknown algorithm '%s'\n", alg_s);
        usage(argv[0]);
    }

    int quantum = 0, cs_cost = 0, aging = 0, verbose = 1;
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--cs") == 0 && i + 1 < argc) {
            if (!parse_nonneg(argv[++i], &cs_cost)) die("--cs needs a number >= 0");
        } else if (strcmp(argv[i], "--aging") == 0 && i + 1 < argc) {
            if (!parse_nonneg(argv[++i], &aging)) die("--aging needs a number >= 0");
        } else if (strcmp(argv[i], "--quiet") == 0) {
            verbose = 0;
        } else if (quantum == 0 && parse_nonneg(argv[i], &quantum)) {
            if (quantum == 0) die("quantum must be >= 1");
        } else {
            fprintf(stderr, "error: unexpected argument '%s'\n", argv[i]);
            usage(argv[0]);
        }
    }

    int need_q = all_mode || single == ALG_RR;
    if (need_q && quantum <= 0) die("algorithm '%s' needs a quantum, e.g. %s %s %s 2",
                                    alg_s, argv[0], input, alg_s);

    static job_table_t table;
    jobs_load(input, &table);

    /* Which algorithms to run. */
    algo_t list[ALG_COUNT];
    int n_list = 0;
    if (all_mode) {
        list[n_list++] = ALG_FCFS;
        list[n_list++] = ALG_SJF;
        list[n_list++] = ALG_SRTF;
        list[n_list++] = ALG_RR;
        if (table.have_priority) list[n_list++] = ALG_PRIO;
    } else {
        list[n_list++] = single;
    }

    /* Fork the logger before any thread exists. */
    logger_start(LOG_FILE);

    run_result_t results[ALG_COUNT];
    memset(results, 0, sizeof(results));

    printf("Loaded %d job(s) from %s\n", table.n, input);

    for (int k = 0; k < n_list; ++k) {
        sched_opts_t opts = {
            .algo    = list[k],
            .quantum = (list[k] == ALG_RR) ? quantum : 0,
            .cs_cost = cs_cost,
            .aging   = (list[k] == ALG_PRIO) ? aging : 0,
            .verbose = verbose,
        };

        run_result_t tmp = { .algo = list[k], .quantum = opts.quantum };
        char label[64];
        algo_label(&tmp, opts.aging, label, sizeof(label));

        printf("\n========== %s ==========\n", label);
        fflush(stdout);
        char hdr[96];
        snprintf(hdr, sizeof(hdr), "===== %s : %s =====", label, input);
        log_text(hdr);

        sched_run(&table, &opts, &g_run_out);

        output_gantt(g_run_out.segs, g_run_out.n_segs);
        output_job_table(&g_run_out.result);
        results[k] = g_run_out.result;
    }

    if (all_mode) {
        printf("\nComparison (%s):", input);
        output_comparison(results, n_list, aging);
    }
    fflush(stdout);

    /* Stop the logger first so the report child does not inherit the pipe. */
    logger_stop();
    printf("\nEvent log written to %s\n", LOG_FILE);

    char rpath[512];
    report_path_from(argv[0], rpath, sizeof(rpath));
    report_publish(rpath, input, results, n_list, aging);

    return 0;
}
