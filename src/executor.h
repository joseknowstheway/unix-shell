#ifndef EXECUTOR_H
#define EXECUTOR_H

/*
 * executor.h — runs parsed commands (Stages 1–5).
 *
 * This is where the shell touches the kernel: the core process pattern (fork /
 * execvp / waitpid), pipelines (pipe + dup2), redirection (open + dup2), and
 * background execution (fork without waiting, tracked as a job).
 */

#include "parser.h"
#include "shell.h"

/*
 * execute_pipeline
 *   pipeline : one or more parsed commands (pipeline->num_commands >= 1). If
 *              pipeline->background is set, runs detached and records a job.
 *   state    : shell state — threaded through so a built-in in a pipeline stage
 *              can reach history/jobs, and so background jobs can be recorded.
 *
 * Foreground: runs every stage concurrently, connecting stage i's stdout to
 * stage i+1's stdin via a kernel pipe, waits for them all, and returns the LAST
 * stage's exit status (bash's "$?" convention). A lone command is just a
 * pipeline of length 1.
 *
 * Background: forks the stages, records a job, prints "[id] pid", and returns 0
 * immediately without waiting.
 *
 * Returns the exit status (foreground) or 0 (background); -1 on a setup failure.
 */
int execute_pipeline(const pipeline_t *pipeline, shell_state_t *state);

#endif /* EXECUTOR_H */
