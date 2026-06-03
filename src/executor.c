/*
 * executor.c — fork/exec/wait, pipelines, redirection, background (Stages 1–5).
 *
 * Stage 1 recap — why three system calls instead of one:
 *
 *   fork()   duplicates the calling process; returns 0 in the child, the child's
 *            PID in the parent. Now two processes run the same code.
 *   execvp() throws away the caller's program image and loads a new one; it does
 *            NOT return on success, so any code after it means exec failed.
 *   waitpid() blocks the parent until a child terminates and reports how it died.
 *
 * Stage 2 — pipelines. A pipe is a one-way kernel buffer with two ends:
 * pipe(fds) gives fds[0] (read) and fds[1] (write). We run every stage at once,
 * wire each stage's stdout to the next stage's stdin via dup2, and obey the
 * golden rule of pipes: close every pipe fd you don't use, in BOTH child and
 * parent, or the reader never sees EOF and the pipeline hangs.
 *
 * Stage 5 — background. spawn_pipeline forks (and wires) every stage but does
 * NOT wait; the foreground path then waits for them, while the background path
 * records a job and returns to the prompt immediately. Foreground waiting blocks
 * SIGCHLD so the async reaper (signals.c) can't snatch our children out from
 * under our waitpid.
 */

#include "executor.h"
#include "builtins.h"
#include "signals.h"

#include <errno.h>     /* errno */
#include <fcntl.h>     /* open, O_* flags */
#include <signal.h>    /* sigprocmask, sigemptyset (reset child mask) */
#include <stdio.h>     /* fprintf, printf, snprintf */
#include <stdlib.h>    /* exit, EXIT_FAILURE */
#include <string.h>    /* strerror */
#include <sys/wait.h>  /* waitpid, WIFEXITED, WEXITSTATUS, WIFSIGNALED */
#include <unistd.h>    /* fork, execvp, pipe, dup2, close */

/* Permission bits for files created by '>' / '>>': rw-r--r-- before umask. */
#define OUTPUT_FILE_MODE 0644

/*
 * apply_redirection — point the child's stdin/stdout at files, if requested.
 *
 * This is the same dup2 trick that wires pipes, aimed at an open file instead of
 * a pipe end. Called from exec_child AFTER any pipe wiring, so an explicit file
 * redirection correctly overrides the pipeline default (e.g. in "a | b > out",
 * b's stdout goes to the file, not onward). Runs only in the child; on any error
 * it reports and exits so the failure can't leak back into the shell.
 */
static void apply_redirection(const command_t *cmd)
{
    if (cmd->input_file != NULL) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "mysh: %s: %s\n", cmd->input_file, strerror(errno));
            exit(EXIT_FAILURE);
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (cmd->output_file != NULL) {
        /* O_CREAT makes the file if absent; then either truncate to empty (">")
         * or seek to the end before each write (">>"). The flag choice is the
         * entire difference between overwrite and append. */
        int flags = O_WRONLY | O_CREAT | (cmd->append_mode ? O_APPEND : O_TRUNC);
        int fd = open(cmd->output_file, flags, OUTPUT_FILE_MODE);
        if (fd < 0) {
            fprintf(stderr, "mysh: %s: %s\n", cmd->output_file, strerror(errno));
            exit(EXIT_FAILURE);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

/*
 * reset_child_signals — restore default dispositions before exec.
 *
 * The shell sets the interactive signals to SIG_IGN, and exec PRESERVES ignored
 * dispositions (it only resets *caught* signals to default). So a child would
 * inherit "ignore Ctrl+C" unless we explicitly restore SIG_DFL here — which is
 * exactly what lets Ctrl+C/Ctrl+Z act on the foreground program.
 */
static void reset_child_signals(void)
{
    signal(SIGINT,  SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
}

/*
 * exec_child — replace the current (child) process with cmd's program.
 *
 * Called only in a child, after any pipe fds have been rewired. Never returns:
 * on success the image is replaced; on failure it reports and exits, so a failed
 * child can never fall back into the shell's REPL and become a second shell.
 */
static void exec_child(const command_t *cmd, shell_state_t *state)
{
    /* The shell forks children with SIGCHLD blocked (foreground/background
     * critical sections). Reset the mask so the new program starts with a clean
     * signal state, as it would under any normal shell. exec preserves the
     * signal mask, so we must clear it here ourselves. */
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);

    /* Restore default signal handlers (see reset_child_signals). */
    reset_child_signals();

    /* Redirect files last so they win over any pipe wiring already in place. */
    apply_redirection(cmd);

    /* A built-in reached here is one stage of a pipeline (e.g. "history | grep
     * x"). It runs in THIS child, so its output flows through the pipe and any
     * shell-state change (cd, export) dies with the child — bash's subshell
     * semantics. The single-command case is handled in the shell process by
     * main, never here. */
    if (is_builtin(cmd->args[0])) {
        exit(run_builtin(cmd, state));
    }

    execvp(cmd->args[0], cmd->args);

    /* Reached only if execvp failed. We print the command name ourselves (plain
     * perror couldn't) so it reads like a real shell. */
    fprintf(stderr, "mysh: %s: %s\n", cmd->args[0], strerror(errno));
    exit(EXIT_FAILURE);
}

/*
 * status_to_code — translate waitpid's packed status word into a conventional
 * shell exit code: the program's own code on a normal exit, or 128 + signal
 * number if a signal killed it (so Ctrl+C -> SIGINT(2) -> 130).
 */
static int status_to_code(int status)
{
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 0;
}

/*
 * spawn_pipeline — fork and wire every stage; fill pids[]; do NOT wait.
 *
 * This is the shared engine for both foreground and background execution: the
 * only difference between them is what the caller does afterward (wait, or
 * record a job). Returns the number of stages forked, or -1 on a setup error.
 */
static int spawn_pipeline(const pipeline_t *pipeline, shell_state_t *state,
                          pid_t pids[])
{
    int n = pipeline->num_commands;

    /* prev_read holds the read end of the PREVIOUS command's output pipe, which
     * becomes the CURRENT command's stdin. -1 means "no upstream" (first cmd). */
    int prev_read = -1;

    for (int i = 0; i < n; i++) {
        int is_last = (i == n - 1);

        /* Every command except the last needs a fresh pipe to feed the next. */
        int pipefd[2] = { -1, -1 };
        if (!is_last && pipe(pipefd) < 0) {
            perror("mysh: pipe");
            return -1;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("mysh: fork");
            return -1;
        }

        if (pid == 0) {
            /* ── CHILD i ──
             * Join the job's process group so Ctrl+C/Ctrl+Z reach the whole
             * pipeline as a unit. The first stage CREATES the group (its pid
             * becomes the pgid); later stages join it. Both child and parent
             * call setpgid to close the fork/exec race (whichever wins, the
             * value is the same); errors are ignored. */
            pid_t pgid = (i == 0) ? 0 : pids[0];
            setpgid(0, pgid);

            /* Wire stdin to the upstream pipe (if any), stdout to our own output
             * pipe (if we're not last), then close every raw pipe fd we still
             * hold: once dup2 has copied an fd onto STDIN/STDOUT, the original
             * number is redundant, and leaving it open would keep a pipe end
             * alive and stall EOF downstream. */
            if (prev_read != -1) {
                dup2(prev_read, STDIN_FILENO);
                close(prev_read);
            }
            if (!is_last) {
                close(pipefd[0]);              /* we don't read our own output */
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
            }
            exec_child(&pipeline->commands[i], state);
        }

        /* ── PARENT ──
         * Mirror the child's setpgid (race-free group assignment), then drop our
         * own copies of the pipe fds immediately, or those open fds would hold
         * pipe ends open and prevent downstream readers from ever seeing EOF. */
        pids[i] = pid;
        setpgid(pid, (i == 0) ? pid : pids[0]); /* ignore EACCES if child exec'd */
        if (prev_read != -1) {
            close(prev_read);                  /* fully handed off to child i */
        }
        if (!is_last) {
            close(pipefd[1]);                  /* parent never writes */
            prev_read = pipefd[0];             /* hand read end to child i+1 */
        }
    }

    return n;
}

/*
 * format_pipeline_label — render a pipeline back into a readable string for the
 * jobs listing, e.g. "sleep 5" or "ls -la | grep .c". snprintf keeps it within
 * the buffer; an over-long pipeline is simply truncated.
 */
static void format_pipeline_label(const pipeline_t *pipeline, char *buf,
                                  size_t size)
{
    size_t used = 0;
    buf[0] = '\0';

    for (int c = 0; c < pipeline->num_commands && used < size; c++) {
        if (c > 0) {
            int n = snprintf(buf + used, size - used, " | ");
            if (n < 0) return;
            used += (size_t)n;
        }
        const command_t *cmd = &pipeline->commands[c];
        for (int a = 0; a < cmd->argc && used < size; a++) {
            int n = snprintf(buf + used, size - used, "%s%s",
                             (a > 0) ? " " : "", cmd->args[a]);
            if (n < 0) return;
            used += (size_t)n;
        }
    }
}

int wait_foreground_group(shell_state_t *state, pid_t pgid, const char *label)
{
    int last_status = 0;
    int interrupted = 0; /* job killed by Ctrl+C / Ctrl+\ — needs a fresh line */

    for (;;) {
        int   status;
        /* Wait for ANY member of the foreground group. WUNTRACED also reports a
         * child that STOPPED (Ctrl+Z), which a plain wait would miss. */
        pid_t pid = waitpid(-pgid, &status, WUNTRACED);
        if (pid < 0) {
            break; /* ECHILD: no members remain — the job is finished */
        }

        if (WIFSTOPPED(status)) {
            /* Ctrl+Z stops the entire foreground group at once. Record (or, for
             * a resumed job, re-mark) it as stopped so fg/bg can revive it. */
            job_t *job = jobs_find_by_pid(&state->jobs, pgid);
            int job_id;
            if (job != NULL) {
                job->state = JOB_STOPPED;
                job_id = job->job_id;
            } else {
                job_id = jobs_add(&state->jobs, pgid, label, JOB_STOPPED);
            }
            printf("\n[%d]  Stopped  %s\n", job_id, label);
            return 128 + WSTOPSIG(status);
        }

        /* A member terminated. Keep the most recent status as the result; for a
         * single command that's exact, for a pipeline it's the last to finish. */
        if (WIFSIGNALED(status) &&
            (WTERMSIG(status) == SIGINT || WTERMSIG(status) == SIGQUIT)) {
            interrupted = 1;
        }
        last_status = status_to_code(status);
    }

    /* The terminal echoed "^C"/"^\" with no newline, so move to a fresh line
     * before the next prompt — exactly what bash does. Interactive only, so
     * scripted (piped) output is byte-for-byte unchanged. */
    if (interrupted && state->interactive) {
        printf("\n");
    }

    /* Fully finished: if this was a tracked (resumed) job, drop it. */
    jobs_remove_by_pid(&state->jobs, pgid);
    return last_status;
}

int execute_pipeline(const pipeline_t *pipeline, shell_state_t *state)
{
    pid_t pids[MAX_COMMANDS];

    char label[JOB_CMD_LEN];
    format_pipeline_label(pipeline, label, sizeof label);

    if (pipeline->background) {
        /* Block SIGCHLD across spawn+record so the reaper can't fire and try to
         * mark this job done before it has been entered into the table. */
        sigset_t prev;
        block_sigchld(&prev);

        if (spawn_pipeline(pipeline, state, pids) < 0) {
            unblock_sigchld(&prev);
            return -1;
        }

        /* The job's pid is its process group id (the first stage). */
        pid_t pgid = pids[0];
        int job_id = jobs_add(&state->jobs, pgid, label, JOB_RUNNING);

        unblock_sigchld(&prev);

        if (job_id < 0) {
            fprintf(stderr, "mysh: too many background jobs\n");
        } else {
            printf("[%d] %d\n", job_id, (int)pgid);
        }
        return 0;
    }

    /* Foreground: block SIGCHLD so the async reaper can't reap our children
     * before our own wait does (which would make waitpid fail with ECHILD and
     * lose the exit status). The forked children reset their own mask before
     * exec, so they're unaffected. */
    sigset_t prev;
    block_sigchld(&prev);

    if (spawn_pipeline(pipeline, state, pids) < 0) {
        unblock_sigchld(&prev);
        return -1;
    }

    pid_t pgid = pids[0];

    /* Hand the terminal to the job so it (not the shell) owns Ctrl+C/Ctrl+Z and
     * may read stdin; reclaim it once the job finishes or stops. tcsetpgrp from
     * the now-background shell would raise SIGTTOU, but the shell ignores it. */
    if (state->interactive) {
        tcsetpgrp(state->shell_terminal, pgid);
    }

    int status = wait_foreground_group(state, pgid, label);

    if (state->interactive) {
        tcsetpgrp(state->shell_terminal, state->shell_pgid);
    }

    unblock_sigchld(&prev);
    return status;
}
