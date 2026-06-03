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

        /* 3. Parse. parse_command tokenizes `line` in place; the resulting
         * pointers borrow from `line`, so `line` must stay alive through step 4
         * (it does — we don't touch it again until the next loop iteration). */
        command_t cmd;
        if (parse_command(line, &cmd) == 0) {
            continue; /* blank line — just re-prompt */
        }

        /* `exit` is handled inline here in Stage 1 so the loop has a clean way
         * to terminate. In Stage 4 it moves into builtins.c alongside cd,
         * history, and export, where it can honor an optional exit code. */
        if (strcmp(cmd.args[0], "exit") == 0) {
            break;
        }

        /* 4. Execute and loop. (The return status is ignored for now; a future
         * "$?" built-in will want it.) */
        execute_command(&cmd);
    }

    free(line);
    return 0;
}
