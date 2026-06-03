/*
 * signals.c — SIGCHLD reaping (Stage 5).
 *
 * The reaper handler runs asynchronously whenever a child terminates. It must:
 *   - reap EVERY finished child with waitpid(-1, WNOHANG) in a loop (a single
 *     SIGCHLD can stand for several children that exited at once; signals don't
 *     queue, so one delivery may cover many deaths),
 *   - mark the matching job done,
 *   - call nothing that isn't async-signal-safe (so: no printf, no malloc) — it
 *     only touches the jobs array via jobs_mark_done,
 *   - preserve errno, since waitpid can change it and we may have interrupted
 *     code mid-syscall.
 *
 * Why a file-static pointer? A signal handler's signature is fixed (it gets only
 * the signal number), so it can't be handed the jobs table as a parameter. We
 * stash a pointer at install time. This is the one spot where the shell uses a
 * global, and it's forced by the signal API.
 */

#include "signals.h"

#include <errno.h>    /* errno */
#include <sys/wait.h> /* waitpid, WNOHANG */
#include <unistd.h>   /* (pid_t) */

static jobs_table_t *g_jobs = NULL;

static void sigchld_handler(int sig)
{
    (void)sig;
    int saved_errno = errno;

    pid_t pid;
    int   status;
    /* WNOHANG => return 0 immediately if no more children are waiting, instead
     * of blocking. Loop so we collect all of them. */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (g_jobs != NULL) {
            jobs_mark_done(g_jobs, pid, status);
        }
    }

    errno = saved_errno;
}

void install_signal_handlers(shell_state_t *state)
{
    g_jobs = &state->jobs;

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    /* SA_RESTART: auto-restart syscalls (like the read under getline) the signal
     * interrupts, instead of failing them with EINTR.
     * SA_NOCLDSTOP: only notify on child TERMINATION, not on stop/continue —
     * Stage 5 has no stopped jobs yet (Stage 6 revisits this). */
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
}

void block_sigchld(sigset_t *prev_mask)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, prev_mask);
}

void unblock_sigchld(const sigset_t *prev_mask)
{
    sigprocmask(SIG_SETMASK, prev_mask, NULL);
}
