/* logger.c - Part 5 of the Mini CPU Scheduler.
 *
 * The scheduler creates a pipe and forks a logger process. In the child the
 * read end of the pipe is dup2()'d onto standard input, so the logger simply
 * reads lines from stdin and appends them to schedule.log.
 */

#define _POSIX_C_SOURCE 200809L

#include "logger.h"
#include "common.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int   g_log_fd  = -1;   /* write end of the pipe (parent only) */
static pid_t g_log_pid = -1;

static const char *TYPE_NAMES[] = {
    "ARRIVED", "STARTED", "RESUMED", "PREEMPTED", "FINISHED"
};

/* Logger child: stdin is the pipe. Never returns. */
static void logger_main(const char *path)
{
    FILE *out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "logger: cannot open '%s': %s\n", path, strerror(errno));
        _exit(1);
    }

    char line[256];
    while (fgets(line, sizeof(line), stdin)) {
        fputs(line, out);
        fflush(out);
    }

    fclose(out);
    _exit(0);
}

void logger_start(const char *path)
{
    int fd[2];
    if (pipe(fd) != 0) die("pipe failed: %s", strerror(errno));

    /* Avoid duplicating any unflushed stdio output into the child. */
    fflush(stdout);
    fflush(stderr);

    pid_t pid = fork();
    if (pid < 0) die("fork (logger) failed: %s", strerror(errno));

    if (pid == 0) {
        /* Child: read end becomes stdin; close every pipe fd we do not need. */
        if (dup2(fd[0], STDIN_FILENO) < 0) {
            perror("logger: dup2");
            _exit(1);
        }
        close(fd[0]);
        close(fd[1]);
        logger_main(path);
    }

    /* Parent: keep only the write end. */
    close(fd[0]);
    g_log_fd  = fd[1];
    g_log_pid = pid;

    /* If the logger dies, write() should fail with EPIPE instead of killing us. */
    signal(SIGPIPE, SIG_IGN);
}

static void write_all(const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t w = write(g_log_fd, buf, len);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;     /* logger gone; drop the event */
        }
        buf += w;
        len -= (size_t)w;
    }
}

void log_event(int t, log_type_t type, const char *job_id)
{
    if (g_log_fd < 0) return;
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "[t=%3d] %-9s %s\n",
                     t, TYPE_NAMES[type], job_id);
    if (n > 0) write_all(buf, (size_t)n);
}

void log_text(const char *text)
{
    if (g_log_fd < 0) return;
    write_all(text, strlen(text));
    write_all("\n", 1);
}

void logger_stop(void)
{
    if (g_log_fd >= 0) {
        close(g_log_fd);          /* logger reads EOF and exits */
        g_log_fd = -1;
    }
    if (g_log_pid > 0) {
        int status;
        while (waitpid(g_log_pid, &status, 0) < 0 && errno == EINTR) { }
        g_log_pid = -1;
    }
}
