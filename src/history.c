/*
 * history.c — circular command-history buffer (Stage 4).
 *
 * The ring buffer (`entries`, `next`, `size`) keeps the last HISTORY_CAPACITY
 * commands. `total` counts everything ever entered so the displayed numbers
 * match bash even after old commands scroll out of the buffer.
 */

#include "history.h"

#include <ctype.h>  /* isspace */
#include <stdio.h>  /* printf */
#include <stdlib.h> /* free */
#include <string.h> /* strdup, strlen */

/* True if the string is empty or contains only whitespace. */
static int is_blank(const char *s)
{
    for (; *s != '\0'; s++) {
        if (!isspace((unsigned char)*s)) {
            return 0;
        }
    }
    return 1;
}

void history_add(history_t *h, const char *line)
{
    /* Don't clutter history with blank lines (bash doesn't either). */
    if (line == NULL || is_blank(line)) {
        return;
    }

    char *copy = strdup(line);
    if (copy == NULL) {
        return; /* out of memory — silently skip rather than crash the shell */
    }

    /* Trim the trailing newline (and any other trailing whitespace) so the
     * stored line is clean for display. */
    size_t len = strlen(copy);
    while (len > 0 && isspace((unsigned char)copy[len - 1])) {
        copy[--len] = '\0';
    }

    /* Overwrite the oldest slot if the ring has wrapped around. free(NULL) is
     * safe, so no separate check is needed for the not-yet-full case. */
    free(h->entries[h->next]);
    h->entries[h->next] = copy;

    h->next = (h->next + 1) % HISTORY_CAPACITY;
    if (h->size < HISTORY_CAPACITY) {
        h->size++;
    }
    h->total++;
}

void history_print(const history_t *h)
{
    /* The oldest stored entry sits `size` slots behind the write cursor. */
    int oldest = (h->next - h->size + HISTORY_CAPACITY) % HISTORY_CAPACITY;
    int first_number = h->total - h->size + 1;

    for (int i = 0; i < h->size; i++) {
        int idx = (oldest + i) % HISTORY_CAPACITY;
        printf("%5d  %s\n", first_number + i, h->entries[idx]);
    }
}

void history_free(history_t *h)
{
    for (int i = 0; i < HISTORY_CAPACITY; i++) {
        free(h->entries[i]);
        h->entries[i] = NULL;
    }
    h->size = 0;
    h->next = 0;
    h->total = 0;
}
