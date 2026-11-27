MINI CPU SCHEDULER - Operating Systems Lab Project
==================================================

1. GROUP MEMBERS
----------------
  Name                    Roll number     Parts worked on
  ----------------------  --------------  ---------------------------------------
  <Member 1 name>         <Roll no>       <e.g. Part 1, Part 3>
  <Member 2 name>         <Roll no>       <e.g. Part 2, Part 4>
  <Member 3 name>         <Roll no>       <e.g. Part 5, Part 6, bonus>

  Group leader (submitter): <name>


2. BUILD AND RUN
----------------
  make                     builds ./scheduler and ./report (g++ -Wall -Wextra -pthread)
  make clean               removes binaries and schedule.log

  ./scheduler <input_file> <algorithm> [quantum] [--aging N] [--cs N] [--quiet]
      algorithm : fcfs | sjf | srtf | rr | prio | all
      quantum   : required for rr and all
      --aging N : improve a waiting job's priority by 1 every N units (prio)
      --cs N    : charge N simulated units for each direct job switch
      --quiet   : hide the per-time-unit lines

  Examples:
      ./scheduler inputs/jobs1.txt fcfs
      ./scheduler inputs/jobs1.txt rr 2
      ./scheduler inputs/jobs1.txt all 2
      ./scheduler inputs/starve.txt all 2
      ./scheduler inputs/prio.txt prio --aging 2
      ./scheduler inputs/jobs1.txt rr 2 --cs 1

  Helper targets:
      make run         ./scheduler inputs/jobs1.txt rr 2
      make results     regenerates results.txt (all mode, every input file)
      make starvedemo  bonus: starvation demo on inputs/starve.txt
      make agingdemo   priority scheduling with aging on inputs/prio.txt
      make csdemo      Round Robin with context-switch cost

  One time unit = TIME_UNIT_MS = 200 ms (src/common.h). It can be changed
  at build time: make clean && make CXXFLAGS="-Wall -Wextra -pthread -DTIME_UNIT_MS=50"

  Each run writes the event log to schedule.log (overwritten per run) and,
  at the end, the separate report process prints the final report.


3. SOURCE FILES
---------------
  src/common.h       shared structs, constants, and table printing helpers
  src/scheduler.cpp  Parts 1-4 + Part 6 handoff: job loader, algorithms,
                     dispatcher, arrival thread, job threads, shared memory
  src/logger.cpp     Part 5: pipe + fork + dup2 logger process
  src/report.cpp     Part 6: separate report program (fork + exec)


4. HOW IT WORKS
---------------
  Processes:
    scheduler (main)  - dispatcher + arrival thread + one thread per job
    logger            - fork()ed child; pipe read end dup2()'d onto stdin;
                        reads event lines and writes them to schedule.log
    report            - fork() + exec("./report"); reads final metrics from
                        POSIX shared memory after a semaphore says "ready"

  Every simulated time unit t:
    1. Dispatcher posts `tick_sem`; arrival_thread enqueues every job with
       arrival <= t (logging ARRIVED), then posts `arrivals_done_sem`.
    2. Dispatcher (holding queue_mutex) applies the algorithm: preempt the
       running job if needed (RR quantum expiry or SRTF shorter job), pick
       the next ready job if CPU is free, or record IDLE.
    3. Dispatcher posts `run_sem[i]`; job_thread i wakes, prints
       "[t=  3] P2 running   (remaining 2)", sleeps one time unit
       (usleep), and posts `done_sem`.
    4. Dispatcher wakes from `sem_wait(done_sem)`, updates remaining time,
       completion time, and context-switch count, then increments time.
  Every job thread stays blocked on its own `run_sem[i]` at all other times,
  so only one job runs at a time.


5. SYNCHRONISATION TOOLS
------------------------
  Mutexes (pthread_mutex_t):
    queue_mutex   protects ready_q, job fields (rem, first_start, ct,
                  arrived), Gantt entries, counters, and logger pipe writes.
    print_mutex   serialises stdout printing so lines do not interleave.

  Unnamed POSIX semaphores (sem_init, pshared = 0):
    run_sem[i]         initial 0   dispatcher posts -> job thread i runs 1 unit
    done_sem           initial 0   job thread posts -> dispatcher continues
    tick_sem           initial 0   dispatcher posts -> arrival thread runs
    arrivals_done_sem  initial 0   arrival thread posts -> dispatcher schedules

  Named POSIX semaphore (sem_open):
    /mini_sched_ready  initial 0   scheduler posts after writing shared memory;
                                   report process waits on it before reading

  current_time and stop_threads are written only by the dispatcher while all
  worker threads are blocked on semaphores, preventing data races.

  No busy waiting: all waiting threads block in sem_wait(). usleep() is used
  only to simulate one unit of CPU work (or IDLE).

  Shared memory: POSIX (shm_open + ftruncate + mmap), name /mini_sched_shm,
  holding a plain SharedReport struct (per-job metrics and averages).

  Cleanup: all threads are joined and unnamed semaphores destroyed after
  each run; the logger pipe write end is closed and waitpid() reaps it;
  ./report is reaped with waitpid(); shared memory and named semaphore are
  unmapped, closed, and unlinked.


6. CONVENTIONS AND ASSUMPTIONS
------------------------------
  - Ties: earlier arrival first, then smaller job id (string compare).
  - Round Robin: jobs arriving at time t are enqueued before a job whose
    quantum expires at t. If the quantum expires and nobody is waiting, the
    same job continues with a fresh quantum (no preemption, no switch).
  - SRTF: checked at every time unit; equal remaining time -> running job
    continues.
  - Idle CPU is shown as IDLE in the Gantt chart.
  - Context switch = CPU goes directly from one job to a different job.
    First dispatch and switches to/from IDLE are not counted.
  - Input: "id arrival burst [priority]"; priority defaults to 0; '#' lines and blank lines are ignored; file
    need not be sorted. Bad inputs (missing/empty file, malformed line,
    negative arrival, burst < 1, duplicate id, more than 64 jobs) exit with
    a clear error message.
  - Response time = first CPU allocation - arrival.


7. BONUS
--------
  Starvation demo (inputs/starve.txt): LONG (burst 10) arrives at t=0
  alongside a steady stream of short jobs (burst 2, every 2 units). Under
  SJF and SRTF a shorter job is always ready until t=24, so LONG waits 24
  units and finishes last at t=34. Under FCFS it runs first with waiting 0.
  Run with: make starvedemo


8. KNOWN LIMITATIONS
--------------------
  - At most 64 jobs (MAX_JOBS) and job ids up to 15 characters.
  - Only one scheduler instance should run at a time on a machine, since the
    POSIX shared memory and semaphore names are fixed.
