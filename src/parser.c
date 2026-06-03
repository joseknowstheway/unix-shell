/*
 * parser.c — pipeline splitter + whitespace tokenizer (Stages 1–2).
 *
 * Parsing happens in two passes:
 *
 *   1. parse_pipeline splits the line on '|' into segments.
 *   2. parse_command splits each segment on whitespace into argv tokens.
 *
 * Both passes use strtok_r — the reentrant cousin of strtok. Plain strtok keeps
 * its progress in a single hidden global, so a tokenization running inside
 * another tokenization would clobber it. strtok_r hands that state back to us in
 * an explicit `saveptr`, so the inner pass (parse_command) and the outer pass
 * (parse_pipeline) keep separate bookmarks and never interfere. Each token is a
 * slice of the original buffer (the delimiter is overwritten with '\0'), which
 * is why the input line must be writable and must outlive the parse output.
 */

#include "parser.h"

#include <string.h> /* strtok_r, memset */

/* Whitespace that separates argument tokens: space, tab, trailing newline. */
#define TOKEN_DELIMITERS " \t\r\n"

/* The pipeline operator. */
#define PIPE_DELIMITER "|"

int parse_command(char *segment, command_t *cmd)
{
    /* Start from a clean slate so stale pointers from a previous command can
     * never leak through into this one. */
    memset(cmd, 0, sizeof(*cmd));

    /* strtok_r's bookmark lives here on our stack, not in a global. */
    char *saveptr = NULL;
    char *token = strtok_r(segment, TOKEN_DELIMITERS, &saveptr);
    while (token != NULL && cmd->argc < MAX_ARGS - 1) {
        cmd->args[cmd->argc] = token;
        cmd->argc++;
        token = strtok_r(NULL, TOKEN_DELIMITERS, &saveptr);
    }

    /* execvp() requires the argument vector to end in a NULL sentinel so it
     * knows where the list stops. We reserved a slot for it via MAX_ARGS - 1. */
    cmd->args[cmd->argc] = NULL;

    return cmd->argc;
}

int parse_pipeline(char *line, pipeline_t *pipeline)
{
    memset(pipeline, 0, sizeof(*pipeline));

    /* Outer pass: carve the line into '|'-separated segments. This pass must
     * fully hand each segment to parse_command before pulling the next one — its
     * saveptr is independent of parse_command's, so even nested calls are safe. */
    char *saveptr = NULL;
    char *segment = strtok_r(line, PIPE_DELIMITER, &saveptr);
    while (segment != NULL && pipeline->num_commands < MAX_COMMANDS) {
        command_t *cmd = &pipeline->commands[pipeline->num_commands];

        /* A non-empty segment becomes the next stage of the pipeline. An empty
         * one (e.g. "ls |  | grep") contributes no command — we skip it instead
         * of erroring, a Stage 2 simplification noted in parser.h. */
        if (parse_command(segment, cmd) > 0) {
            pipeline->num_commands++;
        }
        segment = strtok_r(NULL, PIPE_DELIMITER, &saveptr);
    }

    return pipeline->num_commands;
}
