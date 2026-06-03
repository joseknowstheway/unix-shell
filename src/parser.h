#ifndef PARSER_H
#define PARSER_H

/*
 * parser.h — turns a raw input line into a structured command.
 *
 * The shell's job is to take text a human typed ("ls -la /home") and hand the
 * kernel something execvp() can run: a NULL-terminated array of argument
 * strings. The parser is the stage that bridges those two worlds.
 *
 * Stage 1 only splits on whitespace. The input_file / output_file / background
 * fields exist now but stay unused until Stage 3 (redirection) and Stage 5
 * (background jobs) — declaring them up front means the executor's struct never
 * has to change shape as features land.
 */

/*
 * Upper bound on tokens in a single command. 64 is generous for an interactive
 * shell and keeps command_t a fixed-size, stack-friendly struct (no malloc in
 * the hot path). args[] needs one extra slot for the trailing NULL execvp wants.
 */
#define MAX_ARGS 64

/*
 * One parsed command.
 *
 * IMPORTANT — memory ownership: the char* pointers in args (and later
 * input_file/output_file) do NOT own their strings. They point INTO the
 * caller's line buffer, which parse_command() tokenizes in place. The line
 * buffer must therefore outlive every use of the command_t. This avoids a pile
 * of small allocations per command at the cost of one rule the caller must
 * respect: don't free the line until you're done executing.
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
 * parse_command
 *   line : a writable, NUL-terminated input line. Tokenized IN PLACE — the
 *          function writes '\0' separators into it, so it must be mutable and
 *          must stay alive as long as `cmd` is used.
 *   cmd  : output; zeroed and then filled with pointers into `line`.
 *
 * Returns the number of arguments parsed (cmd->argc). A return of 0 means the
 * line was blank or whitespace-only, and the caller should simply re-prompt.
 */
int parse_command(char *line, command_t *cmd);

#endif /* PARSER_H */
