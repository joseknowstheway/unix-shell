#ifndef JOBS_H
#define JOBS_H

/*
 * jobs.h — background job tracking (Stage 5).
 *
 * A foreground command is simple: fork, waitpid, done. A BACKGROUND command
 * ("sleep 5 &") is different — the shell forks it and returns to the prompt
 * without waiting. That orphaned-but-still-ours child has to be tracked so we
 * can list it (`jobs`), wait on it (`fg`), signal it (`kill`), and — crucially —
 * REAP it when it finishes. An un-reaped terminated child becomes a zombie: a
 * kernel process-table entry that lingers until its parent collects the exit
 * status. A shell that never reaps would slowly leak zombies.
 *
 * The table is a fixed array of slots. Each background command gets one slot and
 * a user-facing id ([1], [2], ...). The STOPPED state is unused until Stage 6
 * (Ctrl+Z / job control); Stage 5 uses RUNNING and DONE.
 */

#include <sys/types.h> /* pid_t */

#define MAX_JOBS    64
#define JOB_CMD_LEN 256

typedef enum {
    JOB_RUNNING,
    JOB_DONE,
    JOB_STOPPED /* Stage 6 */
} job_state_t;

typedef struct {
    pid_t       pid;     /* the job's representative pid (last pipeline stage) */
    int         job_id;  /* user-facing number shown as [N] */
    job_state_t state;
    int         status;  /* raw waitpid status once reaped (for future "$?") */
    int         in_use;  /* 1 if this slot holds a live job */
    char        command[JOB_CMD_LEN]; /* human-readable label for listings */
} job_t;

typedef struct {
    job_t jobs[MAX_JOBS];
    int   next_id; /* monotonically increasing id to assign next */
} jobs_table_t;

/* Initialize an empty table (next_id = 1). */
void jobs_init(jobs_table_t *table);

/*
 * jobs_add — record a new background job.
 * Returns the assigned job id, or -1 if the table is full. The caller should
 * have SIGCHLD blocked across the fork+add so the reaper can't try to mark this
 * job done before it exists in the table.
 */
int jobs_add(jobs_table_t *table, pid_t pid, const char *command);

/*
 * jobs_mark_done — flag the job owning `pid` as finished and stash its status.
 * Called FROM the SIGCHLD handler, so it must stay async-signal-safe: it only
 * touches the table (no malloc, no stdio). A pid with no matching job (e.g. a
 * non-representative pipeline stage) is ignored.
 */
void jobs_mark_done(jobs_table_t *table, pid_t pid, int status);

/* Look up a job by its user-facing id; NULL if not found. */
job_t *jobs_find_by_id(jobs_table_t *table, int job_id);

/* Free a slot by id (no-op if absent). */
void jobs_remove_by_id(jobs_table_t *table, int job_id);

/* Print every live job: "[id]  <state>  command" (the `jobs` built-in). */
void jobs_print(const jobs_table_t *table);

/*
 * jobs_notify_completed — print a "[id]  Done  command" line for each finished
 * job and free its slot. Called at prompt time (with SIGCHLD blocked) so the
 * notice never lands in the middle of another command's output.
 */
void jobs_notify_completed(jobs_table_t *table);

#endif /* JOBS_H */
