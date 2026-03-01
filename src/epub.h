#ifndef EPUB_H
#define EPUB_H

#include <stddef.h>

typedef struct {
    char *id;
    char *href;
    char *media_type;
} ManifestItem;

typedef struct {
    char *title;
    char *author;
    int version;                 /* EPUB 2 or 3 */
    char *opf_dir;               /* base dir for resolving relative hrefs */
    char *toc_href;              /* path to NCX or nav.xhtml */
    ManifestItem *manifest;
    int manifest_count;
    int *spine;                  /* indices into manifest[] */
    int spine_count;
    void *zip_archive;           /* zip_t* handle */
} Book;

/* Open and parse an EPUB file. Returns 0 on success. */
int epub_open(Book *book, const char *path);

/* Read a file from the EPUB archive. Returns malloc'd buffer, sets *len.
   The href is resolved relative to the OPF directory. */
char *epub_read_file(Book *book, const char *href, size_t *len);

/* Get the manifest href for a spine index. */
const char *epub_spine_href(Book *book, int spine_index);

/* Find spine index for a given href (ignoring fragment). Returns -1 if not found. */
int epub_find_spine_index(Book *book, const char *href);

/* Close book and free all resources. */
void epub_close(Book *book);

#endif
