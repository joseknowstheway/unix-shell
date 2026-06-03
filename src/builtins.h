#ifndef BUILTINS_H
#define BUILTINS_H

/*
 * builtins.h — commands the shell runs itself (Stage 4).
 *
 * A built-in is a command the shell executes in its OWN process rather than
 * forking a child for. That distinction is not cosmetic — it's required for
 * correctness:
 *
 *   cd      changes the working directory. Run in a child, the chdir() would die
 *           with the child and the shell's directory would never change.
 *   export  edits the environment that child processes inherit — it has to edit
 *           the SHELL's environment to have any effect.
 *   exit    must terminate the shell process, not a transient child.
 *   history reads the shell's own recorded command list.
 *
 * So the executor checks is_builtin() and, for a plain command, runs it in-process
 * via run_builtin(). (Inside a pipeline a built-in still runs in the forked
 * child — matching bash's subshell semantics, where `cd` in a pipeline does not
 * change your shell's directory.)
 */

#include "parser.h"
#include "shell.h"

/* True if `name` is one of the shell's built-in commands. */
int is_builtin(const char *name);

/*
 * run_builtin — execute a built-in and return its exit status (0 = success).
 * The `exit` built-in sets state->should_exit rather than calling exit()
 * directly, so the REPL can unwind and free resources cleanly.
 */
int run_builtin(const command_t *cmd, shell_state_t *state);

#endif /* BUILTINS_H */
