/*
 * jobs.c — the background job table (Stage 5).
 *
 * Two callers touch this table from "different threads of control": ordinary
 * shell code (add/print/remove) and the SIGCHLD handler (mark_done). To keep
 * that safe, the shell-code side blocks SIGCHLD around its accesses (see
 * signals.c), and mark_done — the only function the handler calls — does nothing
 * but read/write the array, so it stays async-signal-safe.
 */

#include "jobs.h"

#include <stdio.h>  /* printf, snprintf */
#include <string.h> /* memset */

void jobs_init(jobs_table_t *table)
{
    memset(table, 0, sizeof(*table));
    table->next_id = 1;
}

int jobs_add(jobs_table_t *table, pid_t pid, const char *command)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!table->jobs[i].in_use) {
            job_t *job = &table->jobs[i];
            job->pid    = pid;
            job->job_id = table->next_id++;
            job->state  = JOB_RUNNING;
            job->status = 0;
            job->in_use = 1;
            snprintf(job->command, JOB_CMD_LEN, "%s", command);
            return job->job_id;
        }
    }
    return -1; /* table full */
}

void jobs_mark_done(jobs_table_t *table, pid_t pid, int status)
{
    /* Async-signal-safe: array scan only, no library calls. */
    for (int i = 0; i < MAX_JOBS; i++) {
        if (table->jobs[i].in_use &&
            table->jobs[i].pid == pid &&
            table->jobs[i].state == JOB_RUNNING) {
            table->jobs[i].state  = JOB_DONE;
            table->jobs[i].status = status;
            return;
        }
    }
}

job_t *jobs_find_by_id(jobs_table_t *table, int job_id)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (table->jobs[i].in_use && table->jobs[i].job_id == job_id) {
            return &table->jobs[i];
        }
    }
    return NULL;
}

void jobs_remove_by_id(jobs_table_t *table, int job_id)
{
    job_t *job = jobs_find_by_id(table, job_id);
    if (job != NULL) {
        job->in_use = 0;
    }
}

/* Human-readable name for a job state, for the `jobs` listing. */
static const char *state_name(job_state_t state)
{
    switch (state) {
        case JOB_RUNNING: return "Running";
        case JOB_DONE:    return "Done";
        case JOB_STOPPED: return "Stopped";
    }
    return "Unknown";
}

void jobs_print(const jobs_table_t *table)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        const job_t *job = &table->jobs[i];
        if (job->in_use) {
            printf("[%d]  %-7s  %s\n", job->job_id, state_name(job->state),
                   job->command);
        }
    }
}

void jobs_notify_completed(jobs_table_t *table)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        job_t *job = &table->jobs[i];
        if (job->in_use && job->state == JOB_DONE) {
            printf("[%d]  Done     %s\n", job->job_id, job->command);
            job->in_use = 0;
        }
    }
}
