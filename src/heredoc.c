/*
 * heredoc.c — collect here-document bodies (Stage 7).
 *
 * For each "<< WORD", read lines from the shell's stdin until a line exactly
 * equal to WORD, writing the body into a temp file. The temp file is unlinked
 * immediately (so it vanishes when closed) and marked close-on-exec, so only the
 * command that dup2's it onto stdin keeps it across exec. The body is taken
 * literally (no $VAR expansion) — a deliberate simplification.
 */

#include "heredoc.h"

#include <fcntl.h>  /* fcntl, F_SETFD, FD_CLOEXEC */
#include <stdio.h>  /* getline, printf, perror */
#include <stdlib.h> /* free */
#include <string.h> /* strcmp, strlen */
#include <unistd.h> /* write, close, unlink, lseek */

/* Read one here-doc body into a fresh temp file; return its fd or -1. */
static int read_one_heredoc(const char *delim, int interactive)
{
    char template[] = "/tmp/mysh_heredoc.XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) {
        perror("mysh: heredoc");
        return -1;
    }
    unlink(template);                  /* auto-remove once all fds are closed */
    fcntl(fd, F_SETFD, FD_CLOEXEC);    /* don't leak into unrelated children */

    char  *line = NULL;
    size_t cap = 0;
    for (;;) {
        if (interactive) {
            printf("> ");
            fflush(stdout);
        }
        ssize_t len = getline(&line, &cap, stdin);
        if (len < 0) {
            break; /* EOF before the delimiter — stop collecting */
        }
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0'; /* compare/store without the newline */
        }
        if (strcmp(line, delim) == 0) {
            break; /* delimiter line — done (not part of the body) */
        }
        if (write(fd, line, strlen(line)) < 0 || write(fd, "\n", 1) < 0) {
            perror("mysh: heredoc");
            break;
        }
    }
    free(line);

    lseek(fd, 0, SEEK_SET); /* rewind so the command reads from the start */
    return fd;
}

int collect_heredocs(pipeline_t *pipeline, shell_state_t *state)
{
    for (int c = 0; c < pipeline->num_commands; c++) {
        command_t *cmd = &pipeline->commands[c];
        if (cmd->heredoc_delim == NULL) {
            continue;
        }
        int fd = read_one_heredoc(cmd->heredoc_delim, state->interactive);
        if (fd < 0) {
            return -1;
        }
        cmd->heredoc_fd = fd;
    }
    return 0;
}

void close_heredocs(pipeline_t *pipeline)
{
    for (int c = 0; c < pipeline->num_commands; c++) {
        if (pipeline->commands[c].heredoc_fd >= 0) {
            close(pipeline->commands[c].heredoc_fd);
            pipeline->commands[c].heredoc_fd = -1;
        }
    }
}
