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
  make                     builds ./scheduler and ./report (gcc -Wall -Wextra -pthread)
  make clean               removes binaries, build/ and schedule.log

  ./scheduler <input_file> <algorithm> [quantum] [--cs N] [--aging N] [--quiet]
      algorithm : fcfs | sjf | srtf | rr | prio | all
      quantum   : required for rr and all
      --cs N    : (bonus) N time units of overhead per context switch
      --aging N : (bonus, prio) priority improves by 1 per N units waited
      --quiet   : hide the per-time-unit lines

  Examples:
      ./scheduler inputs/jobs1.txt fcfs
      ./scheduler inputs/jobs1.txt rr 2
      ./scheduler inputs/jobs1.txt all 2
      ./scheduler inputs/prio.txt prio --aging 4
      ./scheduler inputs/jobs4.txt all 2 --cs 1

  Helper targets:
      make run         ./scheduler inputs/jobs1.txt rr 2
      make results     regenerates results.txt (all mode, every input file)
      make csdemo      bonus: context-switch cost 0 vs 1 for quanta 1, 2, 4
      make agingdemo   bonus: priority scheduling with aging 0 vs 4

  One time unit = TIME_UNIT_MS = 200 ms (src/common.h). It can be changed
  at build time: make clean && make CFLAGS="-Wall -Wextra -pthread -DTIME_UNIT_MS=50"

  Each run writes the event log to schedule.log (overwritten per run) and,
  at the end, the separate report process prints the final report.


3. SOURCE FILES
---------------
  src/common.h, common.c   shared types, constants, die(), algorithm names
  src/jobs.c               Part 1: parse + validate the input file
  src/sched.c              Parts 2-3: algorithms, ready queue, dispatcher,
                           arrival thread, one thread per job
  src/output.c             Part 4: Gantt chart, per-job table, comparison
  src/logger.c             Part 5: pipe + fork + dup2 logger process
  src/shm.c                Part 6: shared memory + fork/exec of ./report
  src/report.c             Part 6: the separate report program
  src/main.c               command line, runs one algorithm or "all"


4. HOW IT WORKS
---------------
  Processes:
    scheduler (main)  - dispatcher + arrival thread + one thread per job
    logger            - fork()ed child; pipe read end dup2()'d onto stdin;
                        reads event lines and writes them to schedule.log
    report            - fork() + exec("./report"); reads final metrics from
                        POSIX shared memory after a semaphore says "ready"

  Every simulated time unit t:
    1. Dispatcher posts `tick`; the arrival thread enqueues every job with
       arrival <= t (and logs ARRIVED), then posts `arrivals_done`.
    2. Dispatcher (holding queue_mtx) applies the algorithm: preempt the
       running job if needed, otherwise pick the next ready job, or IDLE.
    3. Dispatcher posts run[i]; job thread i wakes, prints
       "[t= 3] P2 running (remaining 2)", sleeps one time unit (simulated
       work) and posts `done`.
    4. Dispatcher's sem_wait(done) returns; it updates remaining time,
       completion time, Gantt chart and context-switch count, then t++.
  A job thread is blocked on its own semaphore run[i] at all other times, so
  exactly one job "has the CPU" at any moment.


5. SYNCHRONISATION TOOLS
------------------------
  Mutexes (pthread_mutex_t):
    queue_mtx   protects the ready queue, every run-time field of the job
                table (remaining, first_start, completion, finished,
                in_ready, eff_priority, wait_accum), the Gantt trace, the
                context-switch counter and writes to the logger pipe.
    print_mtx   serialises printing to stdout so lines never interleave.

  Unnamed POSIX semaphores (sem_init, pshared = 0):
    run[i]          initial 0   one per job; dispatcher posts -> job i runs one unit
    done            initial 0   job posts after its unit -> dispatcher continues
    tick            initial 0   dispatcher posts -> arrival thread checks arrivals
    arrivals_done   initial 0   arrival thread posts -> dispatcher schedules

  Named POSIX semaphore (sem_open):
    /mini_sched_ready  initial 0   scheduler posts after writing shared memory;
                                   report process waits on it before reading

  Simulated clock g_t and the stop flag are written only by the dispatcher;
  other threads read them only right after a sem_wait() on a semaphore the
  dispatcher posted, while the dispatcher itself is blocked.

  No busy waiting: every waiting thread blocks in sem_wait(). Sleeping is
  used only to simulate one unit of CPU work (or an IDLE unit).

  Shared memory: POSIX (shm_open + ftruncate + mmap), name /mini_sched_shm,
  holding an shm_report_t (per-job metrics and averages for every run).

  Cleanup: all threads are joined after each run; unnamed semaphores are
  destroyed; the logger is stopped by closing the pipe write end and then
  waitpid(); the report child is waitpid()ed; the shared memory segment is
  munmap()ed and shm_unlink()ed and the named semaphore is sem_unlink()ed.


6. CONVENTIONS AND ASSUMPTIONS
------------------------------
  - Ties: earlier arrival first, then smaller job id (string compare).
  - Round Robin: jobs arriving at time t are enqueued before a job whose
    quantum expires at t. If the quantum expires and nobody is waiting, the
    same job continues (no preemption, no context switch).
  - SRTF: checked at every time unit; equal remaining time -> running job
    continues. The same rule is used for equal priority in Priority mode.
  - Idle CPU is shown as IDLE in the Gantt chart.
  - Context switch = CPU goes directly from one job to a different job.
    The first dispatch and switches to/from IDLE are not counted. A switch
    after a job finishes into the next job is counted.
  - Input: "id arrival burst [priority]"; '#' lines and blank lines are
    ignored; file need not be sorted. Errors (missing/empty file, malformed
    line, negative arrival, burst < 1, duplicate id, more than 64 jobs) stop
    the program with a message naming the line.
  - Response time = first CPU allocation - arrival.
  - In "all" mode the Priority row is added only when the input file has a
    priority column.


7. BONUS (all three)
--------------------
  a) Priority scheduling (algorithm "prio"): preemptive, smaller number =
     higher priority, optional aging (--aging N): for every N units a job
     waits in the ready queue its effective priority improves by 1 (not
     below 0); the boost is reset when the job gets the CPU.
     Demo: make agingdemo  (inputs/prio.txt: P5 response 22 -> 12 with aging 4)

  b) Context-switch cost (--cs N): every counted context switch costs N
     units of overhead, shown as "CS" in the Gantt chart. Completion,
     turnaround and waiting times include this overhead.
     Demo: make csdemo  (RR with quanta 1, 2, 4 and cost 0 vs 1)
     For jobs1, RR q=2 avg waiting goes from 6.00 (cost 0) to 11.67 (cost 1),
     while FCFS only goes from 3.33 to 4.33 because it switches less.

  c) Starvation demo: inputs/starve.txt. LONG (burst 10) arrives at 0 with a
     short job every 2 units. Under SJF and SRTF a shorter job is always
     ready, so LONG waits 24 units and runs last (completion 34); under FCFS
     it runs first with waiting 0.


8. KNOWN LIMITATIONS
--------------------
  - At most 64 jobs (MAX_JOBS) and job ids up to 15 characters.
  - Job arrivals that fall inside a context-switch overhead unit (--cs) are
    enqueued at the end of that overhead.
  - Only one scheduler instance should run at a time on a machine, since the
    shared memory and semaphore names are fixed.
