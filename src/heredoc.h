#ifndef HEREDOC_H
#define HEREDOC_H

/*
 * heredoc.h — here-document collection (Stage 7 stretch goal).
 *
 * "cmd << WORD" feeds the lines that follow — up to a line equal to WORD — into
 * cmd's stdin. Unlike "<file", the data isn't on disk; the shell reads it from
 * its own input right after the command line, stashes it, and hands it to the
 * command as stdin. We stash it in an unlinked temp file and pass its fd.
 */

#include "parser.h"
#include "shell.h"

/*
 * collect_heredocs — for every command with a here-doc, read its body from stdin
 * (up to the delimiter line) into a temp file and store the fd in heredoc_fd.
 * In interactive mode it prints a "> " continuation prompt. Returns 0, or -1 if
 * a temp file couldn't be created.
 */
int collect_heredocs(pipeline_t *pipeline, shell_state_t *state);

/* close_heredocs — close any here-doc fds the shell still holds (after exec). */
void close_heredocs(pipeline_t *pipeline);

#endif /* HEREDOC_H */
