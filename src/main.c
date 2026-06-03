/*
 * main.c — the Read-Eval-Print loop (Stage 1).
 *
 * Every shell, from this one to bash, is the same five-step loop running
 * forever:
 *
 *   1. print a prompt
 *   2. read a line of input
 *   3. parse it into a command
 *   4. execute the command
 *   5. go back to 1
 *
 * Stage 1 builds that skeleton for single commands. Pipes, redirection,
 * built-ins, jobs, and signals all hang off these same five steps in later
 * stages.
 */

#include "parser.h"
#include "executor.h"

#include <stdio.h>  /* printf, fflush, getline */
#include <stdlib.h> /* free */
#include <string.h> /* strcmp */

#define PROMPT "mysh> "

int main(void)
{
    /* getline() manages this buffer for us: pass a NULL pointer and 0 size the
     * first time and it allocates; on later calls it reuses or grows the same
     * buffer. We free it once at the very end. */
    char  *line = NULL;
    size_t line_cap = 0;

    for (;;) {
        /* 1. Prompt. fflush guarantees it appears before we block on input —
         * stdout is line-buffered to a terminal but not when piped, and we want
         * the prompt visible either way. */
        printf(PROMPT);
        fflush(stdout);

        /* 2. Read one line. getline returns the length including the trailing
         * '\n', or -1 at end-of-file. Ctrl+D on an empty line is EOF, which is
         * how a user cleanly exits an interactive shell — we mirror that by
         * printing a newline and stopping. */
        ssize_t len = getline(&line, &line_cap, stdin);
        if (len < 0) {
            printf("\n");
            break;
        }

        /* 3. Parse. parse_pipeline splits `line` on '|' and tokenizes it in
         * place; the resulting pointers borrow from `line`, so `line` must stay
         * alive through step 4 (it does — we don't touch it again until the next
         * loop iteration). */
        pipeline_t pipeline;
        if (parse_pipeline(line, &pipeline) <= 0) {
            /* 0 = blank line; -1 = syntax error (already reported). Either way,
             * nothing to run — just re-prompt. */
            continue;
        }

        /* `exit` is handled inline here so the loop has a clean way to
         * terminate. Only a bare `exit` (a one-command pipeline) counts —
         * `exit | cat` runs exit in a child and must NOT kill the shell. In
         * Stage 4 this moves into builtins.c, where it can honor an exit code. */
        if (pipeline.num_commands == 1 &&
            strcmp(pipeline.commands[0].args[0], "exit") == 0) {
            break;
        }

        /* 4. Execute and loop. (The return status is ignored for now; a future
         * "$?" built-in will want it.) */
        execute_pipeline(&pipeline);
    }

    free(line);
    return 0;
}
