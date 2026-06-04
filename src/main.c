/*
 * main.c — the Read-Eval-Print loop (Stages 1–4).
 *
 * Every shell, from this one to bash, is the same five-step loop running
 * forever:
 *
 *   1. print a prompt
 *   2. read a line of input
 *   3. parse it into a pipeline of commands
 *   4. execute the pipeline
 *   5. go back to 1
 *
 * Stage 4 adds built-in commands. A built-in given as a plain command runs HERE,
 * in the shell process, so its effect (cd, export, exit, history) persists.
 * Built-ins inside a pipeline are handled by the executor in a forked child
 * instead (subshell semantics).
 */

#include "parser.h"
#include "executor.h"
#include "builtins.h"
#include "signals.h"
#include "expand.h"
#include "heredoc.h"
#include "shell.h"

#include <signal.h> /* sigset_t */
#include <stdio.h>  /* printf, fflush, getline */
#include <stdlib.h> /* free */

#define PROMPT "mysh> "

int main(void)
{
    /* All shell-process state in one place (zero-initialized: empty history,
     * status 0, not exiting). */
    shell_state_t state = {0};
    jobs_init(&state.jobs);

    /* Install the SIGCHLD reaper so finished background jobs are collected (no
     * zombies) and marked done. Done after jobs_init so the handler's table
     * pointer is valid. */
    install_signal_handlers(&state);

    /* getline() manages this buffer for us: pass a NULL pointer and 0 size the
     * first time and it allocates; on later calls it reuses or grows the same
     * buffer. We free it once at the very end. */
    char  *line = NULL;
    size_t line_cap = 0;

    for (;;) {
        /* Report any background jobs that finished since the last prompt, and
         * free their slots. SIGCHLD is blocked for a consistent snapshot (the
         * reaper writes the same table). This is why "[1] Done sleep 5" appears
         * at the prompt rather than interrupting other output. */
        sigset_t prev;
        block_sigchld(&prev);
        jobs_notify_completed(&state.jobs);
        unblock_sigchld(&prev);

        /* 1. Prompt. fflush guarantees it appears before we block on input —
         * stdout is line-buffered to a terminal but not when piped, and we want
         * the prompt visible either way. */
        printf(PROMPT);
        fflush(stdout);

        /* 2. Read one line. getline returns the length including the trailing
         * '\n', or -1 at end-of-file. Ctrl+D on an empty line is EOF, which is
         * how a user cleanly exits an interactive shell. */
        ssize_t len = getline(&line, &line_cap, stdin);
        if (len < 0) {
            printf("\n");
            break;
        }

        /* Record the raw line before parsing mutates it. history_add ignores
         * blank lines and takes its own copy. */
        history_add(&state.history, line);

        /* 3. Parse. parse_pipeline splits `line` on '|' and tokenizes it in
         * place; the resulting pointers borrow from `line`, so `line` must stay
         * alive through step 4. */
        pipeline_t pipeline;
        if (parse_pipeline(line, &pipeline) <= 0) {
            /* 0 = blank line; -1 = syntax error (already reported). */
            continue;
        }

        /* 3a. Collect any here-doc bodies (reads more lines from stdin), then
         * expand $VAR / $? and globs across the pipeline. After expansion the
         * args are heap-owned, so free_expansions must run before the next loop. */
        if (collect_heredocs(&pipeline, &state) == 0) {
            expand_pipeline(&pipeline, &state);

            /* 4. Execute.
             * A built-in given as a single, standalone command runs in THIS
             * process so it can change our directory/environment/etc. Anything
             * with a pipe goes to the executor, which runs each stage (built-in
             * or not) in a forked child. */
            if (pipeline.num_commands == 1 &&
                is_builtin(pipeline.commands[0].args[0])) {
                state.last_status = run_builtin(&pipeline.commands[0], &state);
            } else {
                state.last_status = execute_pipeline(&pipeline, &state);
            }

            free_expansions(&pipeline);
        }
        close_heredocs(&pipeline); /* close fds even if a temp file failed */

        /* The `exit` built-in sets this instead of calling exit() directly, so
         * we can fall out of the loop and free everything cleanly. */
        if (state.should_exit) {
            break;
        }
    }

    free(line);
    history_free(&state.history);
    return state.last_status;
}
