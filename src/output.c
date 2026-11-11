/* output.c - Part 4 of the Mini CPU Scheduler: text Gantt chart, per-job
 * metrics table with averages, and the comparison table for "all" mode.
 */

#include "output.h"
#include "common.h"

#include <stdio.h>
#include <string.h>

#define GANTT_WRAP 96   /* max characters per Gantt row before wrapping */

void algo_label(const run_result_t *r, int aging, char *buf, size_t len)
{
    const char *name = ALGO_NAMES[r->algo];
    if (r->algo == ALG_RR)
        snprintf(buf, len, "%s (q = %d)", name, r->quantum);
    else if (r->algo == ALG_PRIO && aging > 0)
        snprintf(buf, len, "%s (aging %d)", name, aging);
    else
        snprintf(buf, len, "%s", name);
}

/* Prints rows like:
 *   | P1    | P2    | P3    |
 *   0       2       4       6
 */
void output_gantt(const gantt_seg_t *segs, int n_segs)
{
    printf("\nGantt chart:\n");
    if (n_segs == 0) { printf("  (empty)\n"); return; }

    int i = 0;
    while (i < n_segs) {
        char bar[GANTT_WRAP + 64];
        char axis[GANTT_WRAP + 64];
        int  bl = 0, al = 0;

        bl += snprintf(bar + bl, sizeof(bar) - bl, "  |");
        al += snprintf(axis + al, sizeof(axis) - al, "  %-*d", 1, segs[i].t_start);

        int row_start = i;
        while (i < n_segs) {
            const gantt_seg_t *s = &segs[i];
            int len = s->t_end - s->t_start;
            /* Width grows with segment length so long bursts look longer. */
            int w = (int)strlen(s->id) + 2;
            if (w < len + 3) w = len + 3;
            if (w > 14) w = 14;

            if (i > row_start && bl + w + 1 > GANTT_WRAP) break;

            int pad = w - (int)strlen(s->id);
            int left = pad / 2, right = pad - left;
            bl += snprintf(bar + bl, sizeof(bar) - bl, "%*s%s%*s|",
                           left, "", s->id, right, "");

            /* Axis: put end time right under the '|' that closes this segment. */
            int target = bl - 1;
            int gap = target - al;
            if (gap < 1) gap = 1;
            al += snprintf(axis + al, sizeof(axis) - al, "%*s%d", gap, "", s->t_end);
            i++;
        }
        printf("%s\n%s\n", bar, axis);
        if (i < n_segs) printf("\n");
    }
}

void output_job_table(const run_result_t *r)
{
    int show_prio = (r->algo == ALG_PRIO);

    printf("\n%-8s %7s %5s", "Job", "Arrival", "Burst");
    if (show_prio) printf(" %4s", "Prio");
    printf(" %10s %10s %7s %8s\n", "Completion", "Turnaround", "Waiting", "Response");

    int width = 8 + 8 + 6 + (show_prio ? 5 : 0) + 11 + 11 + 8 + 9;
    for (int k = 0; k < width; ++k) putchar('-');
    putchar('\n');

    for (int i = 0; i < r->n; ++i) {
        const job_metrics_t *m = &r->m[i];
        printf("%-8s %7d %5d", m->id, m->arrival, m->burst);
        if (show_prio) printf(" %4d", m->priority);
        printf(" %10d %10d %7d %8d\n",
               m->completion, m->turnaround, m->waiting, m->response);
    }

    for (int k = 0; k < width; ++k) putchar('-');
    putchar('\n');
    printf("%-8s %7s %5s", "Average", "", "");
    if (show_prio) printf(" %4s", "");
    printf(" %10s %10.2f %7.2f %8.2f\n",
           "", r->avg_turnaround, r->avg_waiting, r->avg_response);
    printf("Context switches: %d   Total time: %d", r->context_switches, r->total_time);
    if (r->cs_cost > 0) printf("   (switch cost %d unit%s)", r->cs_cost, r->cs_cost == 1 ? "" : "s");
    printf("\n");
}

void output_comparison(const run_result_t *runs, int n_runs, int aging)
{
    printf("\n%-24s %12s %15s %13s %17s\n",
           "Algorithm", "Avg waiting", "Avg turnaround", "Avg response", "Context switches");
    for (int k = 0; k < 24 + 13 + 16 + 14 + 18; ++k) putchar('-');
    putchar('\n');
    for (int i = 0; i < n_runs; ++i) {
        const run_result_t *r = &runs[i];
        if (!r->valid) continue;
        char label[64];
        algo_label(r, aging, label, sizeof(label));
        printf("%-24s %12.2f %15.2f %13.2f %17d\n",
               label, r->avg_waiting, r->avg_turnaround, r->avg_response,
               r->context_switches);
    }
}
