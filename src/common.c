/* common.c - helpers shared by the scheduler and the report program. */

#include "common.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Index must match algo_t. */
const char *ALGO_NAMES[ALG_COUNT] = {
    "FCFS",
    "SJF",
    "SRTF",
    "Round Robin",
    "Priority"
};

void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

int algo_from_string(const char *s, algo_t *out)
{
    if (!s || !out) return 0;
    if (strcmp(s, "fcfs") == 0) { *out = ALG_FCFS; return 1; }
    if (strcmp(s, "sjf")  == 0) { *out = ALG_SJF;  return 1; }
    if (strcmp(s, "srtf") == 0) { *out = ALG_SRTF; return 1; }
    if (strcmp(s, "rr")   == 0) { *out = ALG_RR;   return 1; }
    if (strcmp(s, "prio") == 0) { *out = ALG_PRIO; return 1; }
    return 0;
}
