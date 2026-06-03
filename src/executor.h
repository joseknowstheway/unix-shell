#ifndef EXECUTOR_H
#define EXECUTOR_H

/*
 * executor.h — runs parsed commands (Stages 1–2).
 *
 * This is where the shell touches the kernel. Stage 1 implemented the core
 * process pattern (fork / execvp / waitpid). Stage 2 adds pipelines: chaining
 * several commands so each one's stdout feeds the next one's stdin, wired
 * together with pipe() and dup2().
 */

#include "parser.h"

/*
 * execute_command
 *   cmd : a parsed command with at least one argument (cmd->argc >= 1).
 *
 * Forks a child, replaces it with cmd->args[0], and blocks until it exits.
 * Used directly for the single-command case (a pipeline of length 1).
 *
 * Returns the child's exit status (0–255), 128 + signal number if it was killed
 * by a signal, or -1 if the shell failed to fork.
 */
int execute_command(const command_t *cmd);

/*
 * execute_pipeline
 *   pipeline : one or more parsed commands (pipeline->num_commands >= 1).
 *
 * Runs every command concurrently, connecting command i's stdout to command
 * i+1's stdin through a kernel pipe, then waits for them all.
 *
 * Returns the exit status of the LAST command in the pipeline — the same
 * convention bash uses for "$?" (so "false | true" is success, "true | false"
 * is failure). Returns -1 on a setup failure (pipe/fork).
 */
int execute_pipeline(const pipeline_t *pipeline);

#endif /* EXECUTOR_H */
