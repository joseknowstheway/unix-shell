#ifndef EXPAND_H
#define EXPAND_H

/*
 * expand.h — word expansion (Stage 7 stretch goals).
 *
 * Between parsing and execution, a real shell rewrites the argument words:
 *   - variable expansion: $VAR / ${VAR} -> the environment value, $? -> the last
 *     command's exit status;
 *   - pathname expansion (globbing): *, ?, [..] -> the matching filenames.
 *
 * This is why "echo $HOME" prints your home directory and "ls *.c" lists files
 * the program `ls` never sees the "*" for — the shell did the work first.
 *
 * Memory model change: after expand_pipeline, every args entry and redirection
 * target is a heap-allocated string OWNED by the pipeline (no longer borrowing
 * from the input line, since an expanded word can be longer or split into
 * several words). free_expansions releases them after execution.
 */

#include "parser.h"
#include "shell.h"

/*
 * expand_pipeline — expand variables and globs across every command in place.
 * Replaces each command's args (and input/output redirection targets) with newly
 * allocated, expanded strings. Returns 0 (best effort; on allocation failure a
 * word may be dropped rather than crashing the shell).
 */
int expand_pipeline(pipeline_t *pipeline, shell_state_t *state);

/* free_expansions — free everything expand_pipeline allocated. */
void free_expansions(pipeline_t *pipeline);

#endif /* EXPAND_H */
