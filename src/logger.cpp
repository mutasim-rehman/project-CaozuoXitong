// logger.cpp - Part 5: Logger child process using pipe() + fork() + dup2().

#define _DEFAULT_SOURCE
#include "common.h"

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>

int   log_write_fd = -1;
pid_t log_pid      = -1;

void start_logger(const char* filename) {
    int fd[2];
    if (pipe(fd) != 0) {
        cerr << "error: pipe failed\n";
        exit(1);
    }

    cout.flush();
    cerr.flush();

    pid_t pid = fork();
    if (pid < 0) {
        cerr << "error: fork (logger) failed\n";
        exit(1);
    }

    if (pid == 0) {
        // Child: connect pipe read end to stdin, close unused pipe ends.
        dup2(fd[0], STDIN_FILENO);
        close(fd[0]);
        close(fd[1]);

        ofstream out(filename);
        string line;
        while (getline(cin, line)) {
            out << line << "\n";
            out.flush();
        }
        _exit(0);
    }

    // Parent: keep only the write end.
    close(fd[0]);
    log_write_fd = fd[1];
    log_pid = pid;
    signal(SIGPIPE, SIG_IGN);
}

void log_line(const string& text) {
    if (log_write_fd < 0) return;
    string msg = text + "\n";
    if (write(log_write_fd, msg.c_str(), msg.size()) < 0) { }
}

void log_event(int t, const char* event_name, const char* job_id) {
    log_line("[t=" + to_string(t) + "] " + event_name + " " + job_id);
}

void stop_logger() {
    if (log_write_fd >= 0) {
        close(log_write_fd);     // logger child sees EOF and exits
        log_write_fd = -1;
    }
    if (log_pid > 0) {
        waitpid(log_pid, NULL, 0);
        log_pid = -1;
    }
}
