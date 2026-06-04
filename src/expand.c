/*
 * expand.c — variable and pathname (glob) expansion (Stage 7).
 *
 * Order matters and mirrors a real shell: variables are substituted FIRST, then
 * the result is globbed. So in a word combining $DIR with a star-dot-c pattern,
 * $DIR is substituted first, then the resulting pattern is globbed.
 *
 * Globbing can turn one word into many (or zero) words, so expansion rebuilds
 * each command's argument list. Every produced word is heap-allocated and owned
 * by the pipeline; free_expansions cleans them up after the command runs.
 */

#include "expand.h"

#include <ctype.h>  /* isalpha, isalnum */
#include <glob.h>   /* glob, globfree */
#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* malloc, realloc, free, getenv */
#include <string.h> /* strpbrk, strcmp, strlen */

/* A small growable character buffer for building an expanded word. */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} strbuf_t;

static int sb_init(strbuf_t *sb)
{
    sb->cap = 64;
    sb->len = 0;
    sb->data = malloc(sb->cap);
    return sb->data != NULL;
}

static int sb_putc(strbuf_t *sb, char c)
{
    if (sb->len + 1 >= sb->cap) {
        size_t ncap = sb->cap * 2;
        char *t = realloc(sb->data, ncap);
        if (t == NULL) {
            return 0;
        }
        sb->data = t;
        sb->cap = ncap;
    }
    sb->data[sb->len++] = c;
    return 1;
}

static int sb_puts(strbuf_t *sb, const char *s)
{
    for (; *s != '\0'; s++) {
        if (!sb_putc(sb, *s)) {
            return 0;
        }
    }
    return 1;
}

/*
 * expand_variables — substitute $VAR, ${VAR}, and $? in `in`.
 * Returns a newly allocated string (caller frees), or NULL on allocation
 * failure. An undefined variable expands to the empty string, like bash.
 */
static char *expand_variables(const char *in, const shell_state_t *state)
{
    strbuf_t sb;
    if (!sb_init(&sb)) {
        return NULL;
    }

    for (size_t i = 0; in[i] != '\0';) {
        if (in[i] != '$') {
            sb_putc(&sb, in[i++]);
            continue;
        }

        char next = in[i + 1];
        if (next == '?') {
            /* $? — the last command's exit status. */
            char num[16];
            snprintf(num, sizeof num, "%d", state->last_status);
            sb_puts(&sb, num);
            i += 2;
        } else if (next == '{') {
            /* ${NAME} */
            char name[256];
            size_t k = 0;
            size_t j = i + 2;
            while (in[j] != '\0' && in[j] != '}' && k < sizeof(name) - 1) {
                name[k++] = in[j++];
            }
            name[k] = '\0';
            if (in[j] == '}') {
                const char *v = getenv(name);
                if (v != NULL) {
                    sb_puts(&sb, v);
                }
                i = j + 1;
            } else {
                sb_putc(&sb, '$'); /* unterminated ${ — keep literal */
                i++;
            }
        } else if (isalpha((unsigned char)next) || next == '_') {
            /* $NAME — name is [A-Za-z_][A-Za-z0-9_]* */
            char name[256];
            size_t k = 0;
            size_t j = i + 1;
            while ((isalnum((unsigned char)in[j]) || in[j] == '_') &&
                   k < sizeof(name) - 1) {
                name[k++] = in[j++];
            }
            name[k] = '\0';
            const char *v = getenv(name);
            if (v != NULL) {
                sb_puts(&sb, v);
            }
            i = j;
        } else {
            sb_putc(&sb, '$'); /* lone '$' or '$' before punctuation — literal */
            i++;
        }
    }

    sb_putc(&sb, '\0');
    return sb.data;
}

/* True if the word contains a glob metacharacter worth expanding. */
static int has_glob(const char *s)
{
    return strpbrk(s, "*?[") != NULL;
}

/*
 * expand_word — variable-expand a single word, then glob it to its FIRST match.
 * Used for redirection targets (which must resolve to one filename). Returns a
 * newly allocated string.
 */
static char *expand_word(const char *in, const shell_state_t *state)
{
    char *v = expand_variables(in, state);
    if (v == NULL) {
        return NULL;
    }
    if (has_glob(v)) {
        glob_t g;
        /* GLOB_NOCHECK: if nothing matches, return the pattern itself, so an
         * unmatched glob behaves like a literal filename (bash's default). */
        if (glob(v, GLOB_NOCHECK, NULL, &g) == 0 && g.gl_pathc > 0) {
            char *first = strdup(g.gl_pathv[0]);
            globfree(&g);
            free(v);
            return first;
        }
    }
    return v;
}

int expand_pipeline(pipeline_t *pipeline, shell_state_t *state)
{
    for (int c = 0; c < pipeline->num_commands; c++) {
        command_t *cmd = &pipeline->commands[c];

        char *new_args[MAX_ARGS];
        int   n = 0;

        for (int i = 0; i < cmd->argc && n < MAX_ARGS - 1; i++) {
            char *word = expand_variables(cmd->args[i], state);
            if (word == NULL) {
                continue; /* OOM — drop this word rather than crash */
            }

            if (has_glob(word)) {
                glob_t g;
                if (glob(word, GLOB_NOCHECK, NULL, &g) == 0) {
                    for (size_t m = 0; m < g.gl_pathc && n < MAX_ARGS - 1; m++) {
                        new_args[n++] = strdup(g.gl_pathv[m]);
                    }
                    globfree(&g);
                    free(word);
                } else {
                    new_args[n++] = word; /* glob error — keep the literal word */
                }
            } else {
                new_args[n++] = word;
            }
        }

        /* Replace the (borrowed) arg pointers with our owned, expanded ones. */
        for (int k = 0; k < n; k++) {
            cmd->args[k] = new_args[k];
        }
        cmd->args[n] = NULL;
        cmd->argc = n;

        /* Redirection targets get variable + single-match glob expansion too. */
        if (cmd->input_file != NULL) {
            cmd->input_file = expand_word(cmd->input_file, state);
        }
        if (cmd->output_file != NULL) {
            cmd->output_file = expand_word(cmd->output_file, state);
        }
    }
    return 0;
}

void free_expansions(pipeline_t *pipeline)
{
    for (int c = 0; c < pipeline->num_commands; c++) {
        command_t *cmd = &pipeline->commands[c];
        for (int i = 0; i < cmd->argc; i++) {
            free(cmd->args[i]);
            cmd->args[i] = NULL;
        }
        free(cmd->input_file);
        cmd->input_file = NULL;
        free(cmd->output_file);
        cmd->output_file = NULL;
    }
}
