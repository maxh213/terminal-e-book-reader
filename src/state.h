#ifndef STATE_H
#define STATE_H

typedef struct {
    int spine_index;
    int scroll_line;
} ReadingState;

/* Load reading state for the given file path. Returns 0 on success. */
int state_load(ReadingState *rs, const char *filepath);

/* Save reading state for the given file path. Returns 0 on success. */
int state_save(ReadingState *rs, const char *filepath);

#endif
