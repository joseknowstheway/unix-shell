#ifndef SIGNALS_H
#define SIGNALS_H

/*
 * signals.h — signal handling (Stage 5: SIGCHLD reaping).
 *
 * When a background child terminates, the kernel sends the shell SIGCHLD. We
 * install a handler that reaps every finished child (so none linger as zombies)
 * and marks the matching job done. Stage 6 will extend this file with SIGINT /
 * SIGTSTP / terminal control for full job control.
 *
 * Concurrency: the handler and the main code both touch the jobs table, so the
 * main code blocks SIGCHLD around its table accesses. These helpers wrap the
 * sigprocmask calls that do that.
 */

#include <signal.h> /* sigset_t */

#include "shell.h"

/*
 * install_signal_handlers — install the SIGCHLD reaper.
 * Stashes a pointer to state->jobs for the handler to use, and registers the
 * handler with SA_RESTART (so a background child finishing doesn't make the
 * blocking read under getline fail with EINTR).
 */
void install_signal_handlers(shell_state_t *state);

/*
 * block_sigchld / unblock_sigchld — bracket a critical section that reads or
 * mutates the jobs table, so the reaper can't run in the middle of it.
 * block_sigchld saves the previous mask into *prev_mask; unblock_sigchld
 * restores it.
 */
void block_sigchld(sigset_t *prev_mask);
void unblock_sigchld(const sigset_t *prev_mask);

#endif /* SIGNALS_H */
