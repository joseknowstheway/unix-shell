/*
 * executor.c — the fork/exec/wait core plus pipelines (Stages 1–2).
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
 * pipe(fds) gives fds[0] (read) and fds[1] (write). Anything written to fds[1]
 * comes out of fds[0]. To make "ls | grep" work we run both commands at once,
 * point ls's stdout at a pipe's write end and grep's stdin at the same pipe's
 * read end (via dup2), and let data flow.
 *
 * The make-or-break rule of pipes is FILE-DESCRIPTOR HYGIENE: every process that
 * inherits a copy of a pipe end must close the ends it doesn't use. A pipe's
 * read end only reports EOF once EVERY copy of the write end is closed. If the
 * shell (or any child) leaves a write end open, the reader blocks forever and
 * the pipeline hangs. So we close aggressively, in both children and parent.
 */

#include "executor.h"

#include <errno.h>     /* errno */
#include <fcntl.h>     /* open, O_* flags */
#include <stdio.h>     /* fprintf */
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
 * exec_child — replace the current (child) process with cmd's program.
 *
 * Shared by the single-command and pipeline paths. Called only in a child, only
 * after any file descriptors have already been rewired. Never returns: on
 * success the image is replaced; on failure it reports and exits, so a failed
 * child can never fall back into the shell's REPL and become a second shell.
 */
static void exec_child(const command_t *cmd)
{
    /* Redirect files last so they win over any pipe wiring already in place. */
    apply_redirection(cmd);

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

int execute_command(const command_t *cmd)
{
    pid_t pid = fork();

    if (pid < 0) {
        perror("mysh: fork");
        return -1;
    }

    if (pid == 0) {
        /* ── CHILD ── no fds to rewire for a lone command; just exec. */
        exec_child(cmd);
    }

    /* ── PARENT ── wait for exactly this child. */
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("mysh: waitpid");
        return -1;
    }
    return status_to_code(status);
}

int execute_pipeline(const pipeline_t *pipeline)
{
    int n = pipeline->num_commands;

    /* A pipeline of one is just a normal command — reuse the simple path. */
    if (n == 1) {
        return execute_command(&pipeline->commands[0]);
    }

    /* prev_read holds the read end of the PREVIOUS command's output pipe, which
     * becomes the CURRENT command's stdin. -1 means "no upstream" (first cmd). */
    int   prev_read = -1;
    pid_t pids[MAX_COMMANDS];

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
             * Wire stdin to the upstream pipe (if any), stdout to our own output
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
            exec_child(&pipeline->commands[i]);
        }

        /* ── PARENT ──
         * The child now owns whatever copies it needs. The parent must drop its
         * own copies immediately, or those open fds would hold pipe ends open
         * and prevent the readers downstream from ever seeing EOF. */
        pids[i] = pid;
        if (prev_read != -1) {
            close(prev_read);                  /* fully handed off to child i */
        }
        if (!is_last) {
            close(pipefd[1]);                  /* parent never writes */
            prev_read = pipefd[0];             /* hand read end to child i+1 */
        }
    }

    /* Reap every child. We keep the LAST command's status as the pipeline's
     * result (bash's convention for "$?"); the rest are reaped so they don't
     * linger as zombies. */
    int status = 0;
    for (int i = 0; i < n; i++) {
        int s;
        if (waitpid(pids[i], &s, 0) > 0 && i == n - 1) {
            status = status_to_code(s);
        }
    }
    return status;
}
