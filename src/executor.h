#ifndef EXECUTOR_H
#define EXECUTOR_H

/*
 * executor.h — runs a parsed command (Stage 1).
 *
 * This is where the shell actually touches the kernel. Stage 1 implements the
 * single most important pattern in Unix process management: fork() to make a
 * child, execvp() to turn that child into the requested program, waitpid() in
 * the parent to wait for it to finish.
 */

#include "parser.h"

/*
 * execute_command
 *   cmd : a parsed command with at least one argument (cmd->argc >= 1).
 *
 * Forks a child, replaces it with the program named in cmd->args[0], and blocks
 * until that child exits.
 *
 * Returns the child's exit status (0–255) on a normal exit, 128 + signal number
 * if the child was killed by a signal (the convention every real shell uses),
 * or -1 if the shell itself failed to fork. The caller can stash this for a
 * future "$?" built-in.
 */
int execute_command(const command_t *cmd);

#endif /* EXECUTOR_H */
