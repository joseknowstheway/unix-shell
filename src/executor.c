/*
 * executor.c — the fork/exec/wait core (Stage 1).
 *
 * Why three system calls instead of one? Because Unix deliberately splits
 * "make a new process" from "run a new program":
 *
 *   fork()   duplicates the calling process. After it returns there are TWO
 *            processes executing the same code, distinguished only by fork's
 *            return value: 0 in the child, the child's PID in the parent.
 *
 *   execvp() throws away the calling process's program image and loads a new
 *            one in its place. It does NOT return on success — there is nothing
 *            to return to, the old code is gone. So if the line after execvp
 *            runs at all, the exec failed (usually: command not found).
 *
 *   waitpid() blocks the parent until a specific child terminates and reports
 *            how it died. Without it the shell would race ahead and print the
 *            next prompt before the command produced its output.
 *
 * The split is what makes a shell possible: the child customizes itself (later
 * stages will redirect its file descriptors here) in the window between fork and
 * exec, while the parent stays alive to keep running the shell.
 */

#include "executor.h"

#include <errno.h>     /* errno */
#include <stdio.h>     /* perror, fprintf */
#include <stdlib.h>    /* exit, EXIT_FAILURE */
#include <string.h>    /* strerror */
#include <sys/wait.h>  /* waitpid, WIFEXITED, WEXITSTATUS, WIFSIGNALED */
#include <unistd.h>    /* fork, execvp */

int execute_command(const command_t *cmd)
{
    pid_t pid = fork();

    if (pid < 0) {
        /* fork failed — out of process slots or memory. The shell survives;
         * report and bail without running anything. */
        perror("mysh: fork");
        return -1;
    }

    if (pid == 0) {
        /* ── CHILD ──
         * Replace this process image with the requested program. execvp searches
         * each directory in $PATH for args[0], which is why "ls" works without a
         * full "/bin/ls" path. The args array is already NULL-terminated by the
         * parser, exactly as execvp requires. */
        execvp(cmd->args[0], cmd->args);

        /* Only reached if execvp failed — the program image was never replaced.
         * We print the command name ourselves (perror alone couldn't) so the
         * message reads like a real shell: "mysh: foo: No such file or
         * directory". The failed child must exit here; if it fell through it
         * would re-enter the shell's REPL loop and run as a second shell. */
        fprintf(stderr, "mysh: %s: %s\n", cmd->args[0], strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* ── PARENT ──
     * Block until THIS child finishes. We pass the specific pid (not -1) so we
     * wait for the command we just launched and nothing else. */
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("mysh: waitpid");
        return -1;
    }

    /* Translate the raw status word into a conventional exit code. */
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);        /* normal exit: the code it passed */
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);     /* killed by signal N -> 128 + N */
    }
    return 0;
}
