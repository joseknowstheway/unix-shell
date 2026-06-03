/*
 * parser.c — whitespace tokenizer (Stage 1).
 *
 * strtok() is the classic tool for this: hand it a string and a set of
 * delimiter characters, and it returns one token at a time, overwriting each
 * delimiter it finds with '\0'. That's why the input line must be writable and
 * must outlive the resulting command_t — the tokens are slices of the original
 * buffer, not copies.
 *
 * Later stages will graduate to a smarter scan (pipes, then redirection
 * operators), but for single commands strtok is exactly enough.
 */

#include "parser.h"

#include <string.h> /* strtok */

/* Characters that separate tokens: space, tab, and the line's trailing newline. */
#define TOKEN_DELIMITERS " \t\r\n"

int parse_command(char *line, command_t *cmd)
{
    /* Start every parse from a clean slate so stale pointers from a previous
     * command can never leak through into this one. */
    memset(cmd, 0, sizeof(*cmd));

    cmd->argc = 0;

    /* strtok keeps internal state between calls: the first call takes the
     * string, every subsequent call passes NULL to mean "same string, next
     * token". It returns NULL when the tokens run out. */
    char *token = strtok(line, TOKEN_DELIMITERS);
    while (token != NULL && cmd->argc < MAX_ARGS - 1) {
        cmd->args[cmd->argc] = token;
        cmd->argc++;
        token = strtok(NULL, TOKEN_DELIMITERS);
    }

    /* execvp() requires the argument vector to end in a NULL sentinel so it
     * knows where the list stops. We reserved a slot for it via MAX_ARGS - 1. */
    cmd->args[cmd->argc] = NULL;

    return cmd->argc;
}
