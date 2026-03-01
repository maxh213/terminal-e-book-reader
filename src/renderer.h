#ifndef RENDERER_H
#define RENDERER_H

#include <stddef.h>

/* Attribute flags (bitmask per character) */
#define ATTR_BOLD      (1 << 0)
#define ATTR_ITALIC    (1 << 1)
#define ATTR_UNDERLINE (1 << 2)
#define ATTR_HEADING   (1 << 3)
#define ATTR_LINK      (1 << 4)
#define ATTR_HIGHLIGHT (1 << 5)

typedef struct {
    char *text;        /* UTF-8 text for this line */
    unsigned char *attrs;  /* attribute bitmask per byte position */
    int len;           /* byte length of text */
} RenderLine;

typedef struct {
    char *href;        /* link target href */
    int line_start;    /* first line of link text */
    int col_start;     /* byte offset in that line */
    int line_end;
    int col_end;
    int id;            /* unique link ID for this render */
} Link;

typedef struct {
    char *id;          /* anchor id (from id= or name=) */
    int line;          /* line number where this anchor appears */
} Anchor;

typedef struct {
    RenderLine *lines;
    int line_count;
    int line_cap;
    Link *links;
    int link_count;
    int link_cap;
    Anchor *anchors;
    int anchor_count;
    int anchor_cap;
} RenderResult;

/* Render XHTML content into wrapped terminal lines.
   width is the terminal width to wrap to. */
void render_xhtml(RenderResult *rr, const char *xhtml, size_t len, int width);

/* Find the line number for a given anchor id. Returns -1 if not found. */
int render_find_anchor(RenderResult *rr, const char *id);

/* Free a render result. */
void render_free(RenderResult *rr);

#endif
