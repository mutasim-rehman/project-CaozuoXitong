/* logger.h - Part 5: logger process fed through a pipe. */

#ifndef MINI_SCHED_LOGGER_H
#define MINI_SCHED_LOGGER_H

typedef enum {
    LOG_ARRIVED,
    LOG_STARTED,
    LOG_RESUMED,
    LOG_PREEMPTED,
    LOG_FINISHED
} log_type_t;

/* pipe() + fork(); the child dup2()s the read end onto stdin and appends
   every line it reads to `path`. Call before creating any threads. */
void logger_start(const char *path);

/* Send one event line. No-op if the logger is not running.
   Callers serialise calls by holding the scheduler's queue mutex. */
void log_event(int t, log_type_t type, const char *job_id);

/* Send a free-form line (e.g. a run header). */
void log_text(const char *text);

/* Close the write end (logger sees EOF) and waitpid() for it. */
void logger_stop(void);

#endif
