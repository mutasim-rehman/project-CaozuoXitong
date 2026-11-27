// common.h - Shared structs and table printing for scheduler and report.

#ifndef COMMON_H
#define COMMON_H

#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

using namespace std;

const int MAX_JOBS   = 64;
const int MAX_ID_LEN = 16;
const int MAX_GANTT  = 4096;
const int MAX_RUNS   = 5;

#ifndef TIME_UNIT_MS
#define TIME_UNIT_MS 200   // 1 simulated time unit = 200 ms
#endif

// POSIX shared memory and semaphore names (Part 6)
#define SHM_NAME "/mini_sched_shm"
#define SEM_NAME "/mini_sched_ready"

struct Job {
    char id[MAX_ID_LEN];
    int  at;            // arrival time
    int  bt;            // burst time
    int  priority;      // smaller value = higher priority
    int  waited;        // ready-queue wait since last CPU turn
    int  rem;           // remaining time
    int  first_start;   // first time CPU was given (-1 if not started)
    int  ct;            // completion time
    bool arrived;       // true once pushed into ready queue
};

struct GanttEntry {
    int  start;
    int  end;
    char label[MAX_ID_LEN];   // job id or "IDLE"
};

// Plain structs (char arrays instead of std::string) so they fit in shared memory.
struct JobMetric {
    char id[MAX_ID_LEN];
    int  at, bt, priority, ct, tat, wt, rt;
};

struct RunResult {
    char      algo_name[32];  // e.g. "FCFS", "Round Robin (q = 2)"
    int       n;
    JobMetric m[MAX_JOBS];
    double    avg_wt, avg_tat, avg_rt;
    int       context_switches;
    int       total_time;
};

struct SharedReport {
    int       n_runs;
    char      input_file[128];
    RunResult runs[MAX_RUNS];
};

// Logger functions (implemented in logger.cpp)
void start_logger(const char* filename);
void log_event(int t, const char* event_name, const char* job_id);
void log_line(const string& text);
void stop_logger();

inline void print_job_table(const RunResult& r) {
    cout << "\n"
         << left  << setw(8)  << "Job"
         << right << setw(8)  << "Arrival" << setw(6)  << "Burst"
         << setw(11) << "Completion" << setw(11) << "Turnaround"
         << setw(8)  << "Waiting"    << setw(9)  << "Response" << "\n"
         << "-------------------------------------------------------------\n";

    for (int i = 0; i < r.n; ++i) {
        JobMetric m = r.m[i];
        cout << left  << setw(8)  << m.id
             << right << setw(8)  << m.at  << setw(6)  << m.bt
             << setw(11) << m.ct  << setw(11) << m.tat
             << setw(8)  << m.wt  << setw(9)  << m.rt << "\n";
    }

    cout << "-------------------------------------------------------------\n"
         << fixed << setprecision(2)
         << left  << setw(8)  << "Average"
         << right << setw(25) << ""
         << setw(11) << r.avg_tat << setw(8) << r.avg_wt << setw(9) << r.avg_rt << "\n"
         << "Context switches: " << r.context_switches
         << "   Total time: " << r.total_time << "\n";
}

inline void print_comparison(const RunResult runs[], int n_runs) {
    cout << "\n"
         << left  << setw(24) << "Algorithm"
         << right << setw(13) << "Avg waiting" << setw(16) << "Avg turnaround"
         << setw(14) << "Avg response" << setw(18) << "Context switches" << "\n"
         << "-------------------------------------------------------------------------------------\n"
         << fixed << setprecision(2);

    for (int i = 0; i < n_runs; ++i) {
        cout << left  << setw(24) << runs[i].algo_name
             << right << setw(13) << runs[i].avg_wt
             << setw(16) << runs[i].avg_tat
             << setw(14) << runs[i].avg_rt
             << setw(18) << runs[i].context_switches << "\n";
    }
}

#endif
