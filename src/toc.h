#ifndef TOC_H
#define TOC_H

#include "epub.h"

typedef struct {
    char *label;
    char *href;
    int depth;
    int spine_index;  /* resolved spine index, or -1 */
} TOCEntry;

typedef struct {
    TOCEntry *entries;
    int count;
    int cap;
} TOCList;

/* Parse TOC from book (NCX for EPUB 2, nav.xhtml for EPUB 3) */
void toc_parse(TOCList *toc, Book *book);

/* Free TOC */
void toc_free(TOCList *toc);

#endif
