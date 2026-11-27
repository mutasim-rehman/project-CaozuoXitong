// scheduler.cpp - Mini CPU Scheduler (Parts 1-4 + Part 6 shared memory).
//
// Usage: ./scheduler <input_file> <fcfs|sjf|srtf|rr|all> [quantum] [--quiet]

#define _DEFAULT_SOURCE
#include "common.h"

#include <algorithm>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <pthread.h>
#include <semaphore.h>
#include <sstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// ==================== Shared Simulation State ====================
Job  jobs[MAX_JOBS];
int  num_jobs = 0;

int  ready_q[MAX_JOBS];   // stores job indices
int  rq_size  = 0;

int  current_time = 0;
bool verbose_mode = true;
bool stop_threads = false;
int aging_interval = 0;
int switch_cost = 0;

pthread_t arrival_tid;
pthread_t job_tid[MAX_JOBS];
int       thread_arg[MAX_JOBS];

// Mutexes and semaphores
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t           run_sem[MAX_JOBS];
sem_t           done_sem;
sem_t           tick_sem;
sem_t           arrivals_done_sem;

// ==================== Part 1: Load & Validate Input ====================
void load_jobs(string filename) {
    ifstream file(filename);
    if (!file) {
        cerr << "error: cannot open input file '" << filename << "'\n";
        exit(1);
    }

    num_jobs = 0;
    string line;
    int lineno = 0;

    while (getline(file, line)) {
        lineno++;
        istringstream iss(line);
        string id;
        if (!(iss >> id) || id[0] == '#') continue;   // skip blank or comment

        if (num_jobs >= MAX_JOBS) {
            cerr << "error: too many jobs at line " << lineno << "\n";
            exit(1);
        }

        int at, bt, priority = 0;
        string extra;
        if (!(iss >> at >> bt)) {
            cerr << "error: line " << lineno << ": expected 'job_id arrival burst [priority]'\n";
            exit(1);
        }
        if (iss >> extra && extra[0] != '#') {
            istringstream value(extra);
            char trailing;
            if (!(value >> priority) || (value >> trailing) ||
                (iss >> extra && extra[0] != '#')) {
                cerr << "error: line " << lineno << ": invalid priority\n";
                exit(1);
            }
        }
        if ((int)id.size() >= MAX_ID_LEN) {
            cerr << "error: line " << lineno << ": job id too long\n";
            exit(1);
        }
        if (at < 0 || bt <= 0 || priority < 0) {
            cerr << "error: line " << lineno << ": arrival >= 0, burst >= 1, priority >= 0 required\n";
            exit(1);
        }
        for (int i = 0; i < num_jobs; ++i) {
            if (id == jobs[i].id) {
                cerr << "error: line " << lineno << ": duplicate job id '" << id << "'\n";
                exit(1);
            }
        }

        strcpy(jobs[num_jobs].id, id.c_str());
        jobs[num_jobs].at = at;
        jobs[num_jobs].bt = bt;
        jobs[num_jobs].priority = priority;
        num_jobs++;
    }

    if (num_jobs == 0) {
        cerr << "error: input file '" << filename << "' has no jobs\n";
        exit(1);
    }

    // Sort jobs by arrival time, then job id (bubble sort).
    for (int i = 0; i < num_jobs - 1; ++i) {
        for (int j = 0; j < num_jobs - i - 1; ++j) {
            if (jobs[j].at > jobs[j + 1].at ||
               (jobs[j].at == jobs[j + 1].at && strcmp(jobs[j].id, jobs[j + 1].id) > 0)) {
                Job temp = jobs[j];
                jobs[j] = jobs[j + 1];
                jobs[j + 1] = temp;
            }
        }
    }
}

void reset_jobs() {
    for (int i = 0; i < num_jobs; ++i) {
        jobs[i].rem         = jobs[i].bt;
        jobs[i].first_start = -1;
        jobs[i].ct          = -1;
        jobs[i].arrived     = false;
        jobs[i].waited      = 0;
    }
}

// ==================== Ready Queue & Algorithm Helpers ====================
void rq_push(int job_idx) {
    ready_q[rq_size++] = job_idx;
}

void rq_remove(int pos) {
    for (int i = pos; i < rq_size - 1; ++i)
        ready_q[i] = ready_q[i + 1];
    rq_size--;
}

// Pick position in ready_q of the next job to run.
int pick_next_job(string algo) {
    if (algo == "fcfs" || algo == "rr") return 0;   // FIFO head

    int best = 0;
    for (int p = 1; p < rq_size; ++p) {
        Job b = jobs[ready_q[best]];
        Job c = jobs[ready_q[p]];
        int key_b = (algo == "sjf") ? b.bt :
                    (algo == "prio") ? max(0, b.priority - (aging_interval ? b.waited / aging_interval : 0)) : b.rem;
        int key_c = (algo == "sjf") ? c.bt :
                    (algo == "prio") ? max(0, c.priority - (aging_interval ? c.waited / aging_interval : 0)) : c.rem;

        // Smaller key wins; ties broken by earlier arrival, then smaller job id.
        if (key_c < key_b ||
           (key_c == key_b && c.at < b.at) ||
           (key_c == key_b && c.at == b.at && strcmp(c.id, b.id) < 0)) {
            best = p;
        }
    }
    return best;
}

void append_gantt(GanttEntry gantt[], int& g_count, int t, const char* label) {
    if (g_count > 0 && strcmp(gantt[g_count - 1].label, label) == 0) {
        gantt[g_count - 1].end = t + 1;
    } else if (g_count < MAX_GANTT) {
        gantt[g_count].start = t;
        gantt[g_count].end   = t + 1;
        strcpy(gantt[g_count].label, label);
        g_count++;
    }
}

void print_gantt(GanttEntry gantt[], int g_count) {
    cout << "\nGantt:";
    for (int i = 0; i < g_count; ++i)
        cout << " [" << gantt[i].start << "-" << gantt[i].end << ":" << gantt[i].label << "]";
    cout << "\n";
}

string get_algo_label(string algo, int quantum) {
    if (algo == "fcfs") return "FCFS";
    if (algo == "sjf")  return "SJF";
    if (algo == "srtf") return "SRTF";
    if (algo == "prio") return "Priority";
    return "Round Robin (q = " + to_string(quantum) + ")";
}

// ==================== Part 3: Job & Arrival Threads ====================
void* job_thread(void* arg) {
    int idx = *(int*)arg;
    while (true) {
        sem_wait(&run_sem[idx]);
        if (stop_threads) return NULL;

        if (verbose_mode) {
            pthread_mutex_lock(&print_mutex);
            cout << "[t=" << setw(3) << current_time << "] "
                 << jobs[idx].id << " running   (remaining " << jobs[idx].rem << ")\n";
            cout.flush();
            pthread_mutex_unlock(&print_mutex);
        }

        usleep(TIME_UNIT_MS * 1000);   // simulate 1 time unit of CPU work
        sem_post(&done_sem);
    }
}

void* arrival_thread(void*) {
    while (true) {
        sem_wait(&tick_sem);
        if (stop_threads) return NULL;

        pthread_mutex_lock(&queue_mutex);
        for (int i = 0; i < num_jobs; ++i) {
            if (!jobs[i].arrived && jobs[i].at <= current_time) {
                jobs[i].arrived = true;
                rq_push(i);
                log_event(current_time, "ARRIVED", jobs[i].id);
            }
        }
        pthread_mutex_unlock(&queue_mutex);

        sem_post(&arrivals_done_sem);
    }
}

// ==================== Parts 2 & 3: Dispatcher Loop ====================
void run_scheduler(string algo, int quantum, GanttEntry gantt[], int& g_count, RunResult& res) {
    rq_size      = 0;
    current_time = 0;
    stop_threads = false;
    g_count      = 0;
    res.context_switches = 0;
    reset_jobs();

    // Create semaphores and threads (1 arrival thread + 1 thread per job).
    sem_init(&done_sem, 0, 0);
    sem_init(&tick_sem, 0, 0);
    sem_init(&arrivals_done_sem, 0, 0);
    for (int i = 0; i < num_jobs; ++i) {
        sem_init(&run_sem[i], 0, 0);
        thread_arg[i] = i;
        pthread_create(&job_tid[i], NULL, job_thread, &thread_arg[i]);
    }
    pthread_create(&arrival_tid, NULL, arrival_thread, NULL);

    int running   = -1;   // job currently holding CPU (-1 = free)
    int prev_job  = -1;   // job that ran on the previous unit (-1 = IDLE)
    int q_used    = 0;    // units used in current Round Robin quantum
    int completed = 0;

    while (completed < num_jobs) {
        // Step 1: Wake arrival thread to enqueue jobs arriving at current_time.
        sem_post(&tick_sem);
        sem_wait(&arrivals_done_sem);

        pthread_mutex_lock(&queue_mutex);

        // Step 2: Check preemption.
        if (running != -1) {
            if (algo == "rr" && q_used >= quantum) {
                if (rq_size > 0) {
                    log_event(current_time, "PREEMPTED", jobs[running].id);
                    rq_push(running);
                    running = -1;
                }
                q_used = 0;
            } else if (algo == "srtf" && rq_size > 0) {
                int best_pos = pick_next_job("srtf");
                int best = ready_q[best_pos];
                if (jobs[best].rem < jobs[running].rem) {
                    log_event(current_time, "PREEMPTED", jobs[running].id);
                    rq_remove(best_pos);
                    rq_push(running);
                    running = best;
                }
            } else if (algo == "prio" && rq_size > 0) {
                int best_pos = pick_next_job("prio");
                int best = ready_q[best_pos];
                int effective = max(0, jobs[best].priority -
                    (aging_interval ? jobs[best].waited / aging_interval : 0));
                if (effective < jobs[running].priority) {
                    log_event(current_time, "PREEMPTED", jobs[running].id);
                    rq_remove(best_pos);
                    rq_push(running);
                    running = best;
                }
            }
        }

        // Step 3: If CPU is free, pick the next ready job.
        if (running == -1 && rq_size > 0) {
            int pos = pick_next_job(algo);
            running = ready_q[pos];
            rq_remove(pos);
            q_used = 0;
        }

        // Step 4: Count context switch (only job A -> different job B).
        if (running != -1 && prev_job != -1 && prev_job != running) {
            res.context_switches++;
            if (verbose_mode) {
                pthread_mutex_lock(&print_mutex);
                cout << "[t=" << setw(3) << current_time << "] ---- context switch: "
                     << jobs[prev_job].id << " -> " << jobs[running].id << " ----\n";
                cout.flush();
                pthread_mutex_unlock(&print_mutex);
            }
            if (switch_cost > 0) {
                log_event(current_time, "CONTEXT_SWITCH", jobs[running].id);
                for (int i = 0; i < switch_cost; ++i) {
                    append_gantt(gantt, g_count, current_time, "CS");
                    for (int p = 0; p < rq_size; ++p) jobs[ready_q[p]].waited++;
                    current_time++;
                    usleep(TIME_UNIT_MS * 1000);
                }
                prev_job = -1; // charge this switch only once
                pthread_mutex_unlock(&queue_mutex);
                continue; // process arrivals after the overhead
            }
        }

        // Step 5: Handle IDLE CPU.
        if (running == -1) {
            if (verbose_mode) {
                pthread_mutex_lock(&print_mutex);
                cout << "[t=" << setw(3) << current_time << "] IDLE\n";
                cout.flush();
                pthread_mutex_unlock(&print_mutex);
            }
            append_gantt(gantt, g_count, current_time, "IDLE");
            for (int p = 0; p < rq_size; ++p) jobs[ready_q[p]].waited++;
            current_time++;
            prev_job = -1;
            pthread_mutex_unlock(&queue_mutex);
            usleep(TIME_UNIT_MS * 1000);
            continue;
        }

        // Step 6: Log STARTED/RESUMED, update Gantt, and hand CPU to job thread.
        Job& j = jobs[running];
        j.waited = 0;
        if (j.first_start == -1) {
            j.first_start = current_time;
            log_event(current_time, "STARTED", j.id);
        } else if (prev_job != running) {
            log_event(current_time, "RESUMED", j.id);
        }
        append_gantt(gantt, g_count, current_time, j.id);
        pthread_mutex_unlock(&queue_mutex);

        sem_post(&run_sem[running]);
        sem_wait(&done_sem);

        // Step 7: Update remaining time and completion state.
        pthread_mutex_lock(&queue_mutex);
        j.rem--;
        for (int p = 0; p < rq_size; ++p) jobs[ready_q[p]].waited++;
        q_used++;
        current_time++;
        prev_job = running;
        if (j.rem == 0) {
            j.ct = current_time;
            log_event(current_time, "FINISHED", j.id);
            completed++;
            running = -1;
            q_used  = 0;
        }
        pthread_mutex_unlock(&queue_mutex);
    }

    // Stop and join all threads, then destroy semaphores.
    stop_threads = true;
    for (int i = 0; i < num_jobs; ++i) sem_post(&run_sem[i]);
    sem_post(&tick_sem);
    for (int i = 0; i < num_jobs; ++i) pthread_join(job_tid[i], NULL);
    pthread_join(arrival_tid, NULL);

    for (int i = 0; i < num_jobs; ++i) sem_destroy(&run_sem[i]);
    sem_destroy(&done_sem);
    sem_destroy(&tick_sem);
    sem_destroy(&arrivals_done_sem);

    // Fill metrics summary.
    strcpy(res.algo_name, get_algo_label(algo, quantum).c_str());
    res.n          = num_jobs;
    res.total_time = current_time;

    double sum_wt = 0, sum_tat = 0, sum_rt = 0;
    for (int i = 0; i < num_jobs; ++i) {
        JobMetric& m = res.m[i];
        strcpy(m.id, jobs[i].id);
        m.at  = jobs[i].at;
        m.bt  = jobs[i].bt;
        m.priority = jobs[i].priority;
        m.ct  = jobs[i].ct;
        m.tat = m.ct - m.at;
        m.wt  = m.tat - m.bt;
        m.rt  = jobs[i].first_start - m.at;
        sum_wt  += m.wt;
        sum_tat += m.tat;
        sum_rt  += m.rt;
    }
    res.avg_wt  = sum_wt  / num_jobs;
    res.avg_tat = sum_tat / num_jobs;
    res.avg_rt  = sum_rt  / num_jobs;
}

// ==================== Part 6: Shared-Memory Report Handoff ====================
void send_to_report(string input_file, RunResult runs[], int n_runs) {
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_NAME);

    sem_t* ready = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0600, 0);
    if (ready == SEM_FAILED) { cerr << "error: sem_open failed\n"; exit(1); }

    int fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0 || ftruncate(fd, sizeof(SharedReport)) != 0) {
        cerr << "error: shm_open/ftruncate failed\n";
        exit(1);
    }

    SharedReport* shm = (SharedReport*)mmap(
        NULL, sizeof(SharedReport), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { cerr << "error: mmap failed\n"; exit(1); }

    cout.flush();
    cerr.flush();

    // Start ./report before writing so it blocks on the semaphore.
    pid_t pid = fork();
    if (pid == 0) {
        close(fd);
        execl("./report", "report", (char*)NULL);
        cerr << "error: could not run ./report\n";
        _exit(127);
    }

    shm->n_runs = n_runs;
    strncpy(shm->input_file, input_file.c_str(), sizeof(shm->input_file) - 1);
    shm->input_file[sizeof(shm->input_file) - 1] = '\0';
    for (int i = 0; i < n_runs; ++i) shm->runs[i] = runs[i];

    sem_post(ready);               // tell ./report that data is ready
    waitpid(pid, NULL, 0);         // wait for ./report to finish

    munmap(shm, sizeof(SharedReport));
    close(fd);
    shm_unlink(SHM_NAME);
    sem_close(ready);
    sem_unlink(SEM_NAME);
}

// ==================== Main ====================
GanttEntry gantt[MAX_GANTT];

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "usage: " << argv[0] << " <input_file> <fcfs|sjf|srtf|rr|prio|all> [quantum] [--aging N] [--cs N] [--quiet]\n";
        return 1;
    }

    string input_file = argv[1];
    string algo       = argv[2];
    int    quantum    = 0;

    for (int i = 3; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--quiet") verbose_mode = false;
        else if ((arg == "--aging" || arg == "--cs") && i + 1 < argc) {
            char* end;
            long value = strtol(argv[++i], &end, 10);
            if (*end || value < (arg == "--aging" ? 1 : 0) || value > 1000000) {
                cerr << "error: invalid value for " << arg << "\n";
                return 1;
            }
            if (arg == "--aging") aging_interval = (int)value;
            else switch_cost = (int)value;
        } else if (arg == "--aging" || arg == "--cs") {
            cerr << "error: missing value for " << arg << "\n";
            return 1;
        } else {
            char* end;
            long value = strtol(arg.c_str(), &end, 10);
            if (*end || value < 0 || value > 1000000) {
                cerr << "error: unexpected argument '" << arg << "'\n";
                return 1;
            }
            quantum = (int)value;
        }
    }

    string list[MAX_RUNS];
    int    n_list = 0;
    if (algo == "all") {
        list[0] = "fcfs"; list[1] = "sjf"; list[2] = "srtf"; list[3] = "rr"; list[4] = "prio";
        n_list = 5;
    } else if (algo == "fcfs" || algo == "sjf" || algo == "srtf" || algo == "rr" || algo == "prio") {
        list[0] = algo;
        n_list = 1;
    } else {
        cerr << "error: unknown algorithm '" << algo << "'\n";
        return 1;
    }

    if ((algo == "all" || algo == "rr") && quantum <= 0) {
        cerr << "error: algorithm '" << algo << "' requires a positive quantum\n";
        return 1;
    }

    load_jobs(input_file);
    start_logger("schedule.log");   // fork logger before threads start

    RunResult results[MAX_RUNS];
    cout << "Loaded " << num_jobs << " job(s) from " << input_file << "\n";

    for (int k = 0; k < n_list; ++k) {
        string label = get_algo_label(list[k], quantum);
        cout << "\n========== " << label << " ==========\n";
        cout.flush();
        log_line("===== " + label + " : " + input_file + " =====");

        int g_count = 0;
        run_scheduler(list[k], quantum, gantt, g_count, results[k]);
        print_gantt(gantt, g_count);
        print_job_table(results[k]);
    }

    if (algo == "all") {
        cout << "\nComparison (" << input_file << "):";
        print_comparison(results, n_list);
    }
    cout.flush();

    stop_logger();
    cout << "\nEvent log written to schedule.log\n";

    send_to_report(input_file, results, n_list);
    return 0;
}
