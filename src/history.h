#ifndef HISTORY_H
#define HISTORY_H

/*
 * history.h — command history storage (Stage 4).
 *
 * Stores the most recently entered command lines in a fixed-size ring buffer
 * (circular buffer). A ring is the natural structure for "keep the last N": once
 * full, the oldest entry is overwritten without shifting anything. We also track
 * `total` — every command ever entered — so the `history` listing can number
 * lines the way bash does (the numbers keep climbing even after old lines age
 * out of the buffer).
 */

/* How many recent commands to retain. The spec asks for at least 100. */
#define HISTORY_CAPACITY 1000

typedef struct {
    char *entries[HISTORY_CAPACITY]; /* malloc'd copies; NULL = empty slot */
    int   size;  /* number of entries currently stored (0..CAPACITY) */
    int   next;  /* ring index of the next slot to write */
    int   total; /* total commands ever added — used for display numbering */
} history_t;

/*
 * history_add — record one entered line.
 * Skips blank/whitespace-only lines. Strips the trailing newline. Takes its own
 * copy, so the caller's buffer can be reused or freed afterward. When the ring
 * is full, this frees and reuses the oldest slot.
 */
void history_add(history_t *h, const char *line);

/*
 * history_print — list the stored commands, oldest first, each prefixed with its
 * 1-based command number (matching bash's `history`).
 */
void history_print(const history_t *h);

/*
 * history_free — free every stored copy and reset the buffer. Call at shutdown
 * so the leak checker stays clean.
 */
void history_free(history_t *h);

#endif /* HISTORY_H */
