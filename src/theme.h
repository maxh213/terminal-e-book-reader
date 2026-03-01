#ifndef THEME_H
#define THEME_H

typedef struct {
    char *name;
    short fg, bg;
    short status_fg, status_bg;
    short link_fg, link_bg;
    short heading_fg, heading_bg;
    short highlight_fg, highlight_bg;
    short toc_fg, toc_bg;
    short toc_sel_fg, toc_sel_bg;
} Theme;

/* Color pair IDs */
enum {
    PAIR_TEXT = 1,
    PAIR_STATUS,
    PAIR_LINK,
    PAIR_HEADING,
    PAIR_HIGHLIGHT,
    PAIR_TOC,
    PAIR_TOC_SEL,
    PAIR_COUNT
};

/* Initialize theme system, load builtin themes + user themes */
void theme_init(void);

/* Get current theme */
Theme *theme_current(void);

/* Select theme by name. Returns 0 on success. */
int theme_select_by_name(const char *name);

/* Select theme by index. Returns 0 on success. */
int theme_select(int index);

/* Get theme count and names (for selector popup) */
int theme_count(void);
const char *theme_name(int index);

/* Apply current theme colors to ncurses */
void theme_apply(void);

/* Cleanup */
void theme_cleanup(void);

#endif
