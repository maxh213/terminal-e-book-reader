#ifndef UI_H
#define UI_H

#include "epub.h"
#include "toc.h"
#include "state.h"
#include "config.h"

/* Run the main UI loop. Updates rs with final position on return. */
void ui_run(Book *book, TOCList *toc, ReadingState *rs, Config *cfg);

#endif
