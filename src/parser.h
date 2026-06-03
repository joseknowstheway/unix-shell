#ifndef PARSER_H
#define PARSER_H

/*
 * parser.h — turns a raw input line into structured commands.
 *
 * The shell's job is to take text a human typed ("ls -la | grep .c") and hand
 * the kernel something execvp() can run. There are now two levels of structure:
 *
 *   command_t  — one program plus its arguments (Stage 1).
 *   pipeline_t — a chain of commands joined by '|' (Stage 2). A lone command is
 *                just a pipeline of length 1, so the executor only needs one
 *                entry point.
 *
 * The input_file / output_file / background fields on command_t exist now but
 * stay unused until Stage 3 (redirection) and Stage 5 (background jobs) —
 * declaring them up front means the executor's structs never reshape as features
 * land.
 */

/*
 * Upper bound on tokens in a single command. 64 is generous for an interactive
 * shell and keeps command_t a fixed-size, stack-friendly struct (no malloc in
 * the hot path). args[] needs one extra slot for the trailing NULL execvp wants.
 */
#define MAX_ARGS 64

/* Upper bound on commands in one pipeline ("a | b | c ..."). */
#define MAX_COMMANDS 16

/*
 * One parsed command.
 *
 * IMPORTANT — memory ownership: the char* pointers in args (and later
 * input_file/output_file) do NOT own their strings. They point INTO the
 * caller's line buffer, which the parser tokenizes in place. The line buffer
 * must therefore outlive every use of the command_t. This avoids a pile of
 * small allocations per command at the cost of one rule the caller must respect:
 * don't free (or overwrite) the line until you're done executing.
 */
typedef struct {
    char *args[MAX_ARGS]; /* args[0] = program, ..., args[argc] = NULL */
    int   argc;           /* number of real arguments (excludes the NULL) */
    char *input_file;     /* "< file" target; NULL until Stage 3 */
    char *output_file;    /* "> file" target; NULL until Stage 3 */
    int   append_mode;    /* 1 if ">>", 0 if ">"; unused until Stage 3 */
    int   background;     /* 1 if command ended in "&"; unused until Stage 5 */
} command_t;

/*
 * A pipeline: one or more commands whose stdout/stdin will be chained together
 * with pipes. "ls -la | grep .c | wc -l" parses to num_commands == 3.
 */
typedef struct {
    command_t commands[MAX_COMMANDS];
    int       num_commands;
} pipeline_t;

/*
 * parse_command
 *   segment : a writable, NUL-terminated slice of input (one pipeline stage).
 *             Tokenized IN PLACE on whitespace; must stay alive while `cmd` is
 *             used. Reentrant (uses strtok_r), so it is safe to call from inside
 *             another tokenization pass.
 *   cmd     : output; zeroed and then filled with pointers into `segment`.
 *
 * Returns the number of arguments parsed (cmd->argc); 0 means the segment was
 * blank or whitespace-only.
 */
int parse_command(char *segment, command_t *cmd);

/*
 * parse_pipeline
 *   line     : a writable, NUL-terminated input line. Split on '|' and then
 *              tokenized in place; must outlive `pipeline`.
 *   pipeline : output; zeroed and filled with one command_t per '|'-segment.
 *
 * Returns the number of commands (pipeline->num_commands); 0 means the line was
 * blank. Empty segments (e.g. a stray "ls | | grep") are skipped rather than
 * treated as a syntax error — a deliberate Stage 2 simplification.
 */
int parse_pipeline(char *line, pipeline_t *pipeline);

#endif /* PARSER_H */
