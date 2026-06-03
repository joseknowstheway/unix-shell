/*
 * parser.c — pipeline splitter + whitespace tokenizer + redirection (Stages 1–3).
 *
 * Parsing happens in two passes:
 *
 *   1. parse_pipeline splits the line on '|' into segments.
 *   2. parse_command splits each segment on whitespace, separating real argv
 *      tokens from the redirection operators '<', '>', '>>' and their filenames.
 *
 * Both passes use strtok_r — the reentrant cousin of strtok. Plain strtok keeps
 * its progress in a single hidden global, so a tokenization running inside
 * another tokenization would clobber it. strtok_r hands that state back to us in
 * an explicit `saveptr`, so the inner pass (parse_command) and the outer pass
 * (parse_pipeline) keep separate bookmarks and never interfere. Each token is a
 * slice of the original buffer (the delimiter is overwritten with '\0'), which
 * is why the input line must be writable and must outlive the parse output.
 *
 * Redirection note: operators must be their own whitespace-separated tokens
 * ("ls > out.txt", not "ls >out.txt"). The attached form is a documented Stage 3
 * simplification.
 */

#include "parser.h"

#include <stdio.h>  /* fprintf */
#include <string.h> /* strtok_r, strcmp, memset */

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
        /* A redirection operator consumes the NEXT token as its filename and
         * adds neither to args — the program never sees "<", ">", or the
         * filename; it just finds its stdin/stdout already pointed at the file. */
        /* A lone "&" marks the command (and thus its pipeline) as background.
         * Only meaningful at the very end; we simply record it and don't add it
         * to args. Like the redirection operators, it must be its own token
         * ("sleep 5 &", not "sleep 5&"). */
        if (strcmp(token, "&") == 0) {
            cmd->background = 1;
            token = strtok_r(NULL, TOKEN_DELIMITERS, &saveptr);
            continue;
        }

        int is_input  = (strcmp(token, "<") == 0);
        int is_output = (strcmp(token, ">") == 0);
        int is_append = (strcmp(token, ">>") == 0);

        if (is_input || is_output || is_append) {
            char *filename = strtok_r(NULL, TOKEN_DELIMITERS, &saveptr);
            if (filename == NULL) {
                fprintf(stderr,
                        "mysh: syntax error: expected filename after '%s'\n",
                        token);
                return -1; /* abort this command — caller skips execution */
            }
            if (is_input) {
                cmd->input_file = filename;
            } else {
                cmd->output_file = filename;
                cmd->append_mode = is_append; /* 1 for ">>", 0 for ">" */
            }
        } else {
            cmd->args[cmd->argc] = token;
            cmd->argc++;
        }

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

        int argc = parse_command(segment, cmd);
        if (argc < 0) {
            return -1; /* syntax error already reported — abort whole pipeline */
        }
        /* A non-empty segment becomes the next stage of the pipeline. An empty
         * one (e.g. "ls |  | grep") contributes no command — we skip it instead
         * of erroring, a Stage 2 simplification noted in parser.h. */
        if (argc > 0) {
            pipeline->num_commands++;
        }
        segment = strtok_r(NULL, PIPE_DELIMITER, &saveptr);
    }

    /* The "&" lives on the last command (it comes after the last '|'); promote
     * it to a pipeline-wide flag so the executor backgrounds the whole thing. */
    if (pipeline->num_commands > 0) {
        pipeline->background =
            pipeline->commands[pipeline->num_commands - 1].background;
    }

    return pipeline->num_commands;
}
