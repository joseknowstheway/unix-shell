/*
 * builtins.c — the in-process commands (Stage 4).
 *
 * Each built-in is a small function returning a shell-style exit status (0 =
 * success, non-zero = failure). They run in the shell's own process, so their
 * side effects (directory, environment, history, termination) persist — which is
 * the entire reason they can't be forked.
 */

#include "builtins.h"

#include <errno.h>  /* errno */
#include <stdio.h>  /* fprintf, printf */
#include <stdlib.h> /* getenv, setenv, atoi */
#include <string.h> /* strcmp, strchr */
#include <unistd.h> /* chdir */

/*
 * builtin_cd — change the working directory.
 * With no argument, go to $HOME (like bash). chdir() mutating the shell's own
 * cwd is exactly why this can't run in a child.
 */
static int builtin_cd(char *const *args)
{
    const char *dir = args[1] ? args[1] : getenv("HOME");
    if (dir == NULL) {
        fprintf(stderr, "mysh: cd: HOME not set\n");
        return 1;
    }
    if (chdir(dir) != 0) {
        fprintf(stderr, "mysh: cd: %s: %s\n", dir, strerror(errno));
        return 1;
    }
    return 0;
}

/*
 * builtin_exit — request shell termination.
 * Rather than calling exit() here (which would skip the REPL's cleanup and make
 * the leak checker complain), we set a flag the main loop honors, then unwind
 * normally. With no argument we exit with the last command's status, like bash.
 */
static int builtin_exit(const command_t *cmd, shell_state_t *state)
{
    int code = cmd->args[1] ? atoi(cmd->args[1]) : state->last_status;
    state->should_exit = 1;
    return code;
}

/*
 * builtin_export — set an environment variable for future child processes.
 * Parses NAME=VALUE and calls setenv on the SHELL's environment; children then
 * inherit it across fork/exec.
 */
static int builtin_export(char *const *args)
{
    if (args[1] == NULL) {
        fprintf(stderr, "mysh: export: usage: export NAME=VALUE\n");
        return 1;
    }

    char *equals = strchr(args[1], '=');
    if (equals == NULL) {
        fprintf(stderr, "mysh: export: %s: not a NAME=VALUE pair\n", args[1]);
        return 1;
    }

    /* Temporarily split the token at '=' so we have two C strings, then restore
     * it (the token is a slice of the input line; leaving it intact avoids
     * surprising anything that re-reads the line). */
    *equals = '\0';
    int rc = setenv(args[1], equals + 1, 1); /* 1 = overwrite if present */
    *equals = '=';

    if (rc != 0) {
        perror("mysh: export");
        return 1;
    }
    return 0;
}

/* builtin_help — list the available built-ins. */
static int builtin_help(void)
{
    printf("mysh built-in commands:\n");
    printf("  cd [dir]          change the working directory (default: $HOME)\n");
    printf("  exit [code]       exit the shell (default: last command's status)\n");
    printf("  export NAME=VALUE set an environment variable for child processes\n");
    printf("  history           list previously entered commands\n");
    printf("  help              show this message\n");
    printf("\n");
    printf("Everything else is run as an external program via fork/exec.\n");
    return 0;
}

int is_builtin(const char *name)
{
    return strcmp(name, "cd") == 0 ||
           strcmp(name, "exit") == 0 ||
           strcmp(name, "export") == 0 ||
           strcmp(name, "history") == 0 ||
           strcmp(name, "help") == 0;
}

int run_builtin(const command_t *cmd, shell_state_t *state)
{
    const char *name = cmd->args[0];

    if (strcmp(name, "cd") == 0) {
        return builtin_cd(cmd->args);
    }
    if (strcmp(name, "exit") == 0) {
        return builtin_exit(cmd, state);
    }
    if (strcmp(name, "export") == 0) {
        return builtin_export(cmd->args);
    }
    if (strcmp(name, "history") == 0) {
        history_print(&state->history);
        return 0;
    }
    if (strcmp(name, "help") == 0) {
        return builtin_help();
    }

    /* Unreachable if callers gate on is_builtin(), but fail safe. */
    fprintf(stderr, "mysh: %s: not a built-in\n", name);
    return 1;
}
