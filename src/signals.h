#ifndef SIGNALS_H
#define SIGNALS_H

/*
 * signals.h — signal handling & job control setup (Stages 5–6).
 *
 * Stage 5: a SIGCHLD handler reaps finished children (no zombies) and marks jobs
 * done. Stage 6: the shell also IGNORES the interactive signals (SIGINT,
 * SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU) so Ctrl+C / Ctrl+Z reach the foreground
 * job's process group rather than the shell, and it takes ownership of the
 * controlling terminal.
 *
 * Concurrency: the handler and the main code both touch the jobs table, so the
 * main code blocks SIGCHLD around its table accesses. These helpers wrap the
 * sigprocmask calls that do that.
 */

#include <signal.h> /* sigset_t */

#include "shell.h"

/*
 * install_signal_handlers — set up all signal handling and job control.
 * Detects whether stdin is a terminal (state->interactive), puts the shell in
 * its own process group and takes the terminal (when interactive), sets the
 * interactive signals to SIG_IGN, and installs the SIGCHLD reaper with
 * SA_RESTART (so a finishing background child doesn't make the read under
 * getline fail with EINTR). Stashes &state->jobs for the handler.
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
