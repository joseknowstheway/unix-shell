#ifndef SHELL_H
#define SHELL_H

/*
 * shell.h — the shell's own mutable state (Stage 4+).
 *
 * Some commands act on the shell process itself, not on a child: `cd` changes
 * our working directory, `export` changes our environment, `history` reads our
 * recorded commands, `exit` ends our loop. That state lives here, in one struct
 * threaded explicitly through the executor instead of scattered globals — easier
 * to reason about and to test. (Stage 5's jobs table will join it.)
 */

#include "history.h"
#include "jobs.h"

typedef struct {
    history_t    history;     /* recently entered command lines */
    jobs_table_t jobs;        /* background jobs (Stage 5) */
    int          last_status; /* exit status of the last command (future "$?") */
    int          should_exit; /* set by the `exit` built-in to end the REPL */
} shell_state_t;

#endif /* SHELL_H */
