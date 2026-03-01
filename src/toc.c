#include "toc.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/HTMLparser.h>
#include <libxml/tree.h>

static void toc_add(TOCList *toc, const char *label, const char *href, int depth, Book *book) {
    if (toc->count >= toc->cap) {
        toc->cap = toc->cap ? toc->cap * 2 : 32;
        toc->entries = xrealloc(toc->entries, (size_t)toc->cap * sizeof(TOCEntry));
    }
    TOCEntry *e = &toc->entries[toc->count++];
    e->label = xstrdup(label);
    e->href = xstrdup(href);
    e->depth = depth;
    e->spine_index = epub_find_spine_index(book, href);
}

/* Get all text content from a node */
static char *get_text_content(xmlNodePtr node) {
    xmlChar *text = xmlNodeGetContent(node);
    if (!text) return xstrdup("");
    char *s = xstrdup((char *)text);
    xmlFree(text);
    return str_trim(s);
}

/* ---- EPUB 2: parse NCX ---- */

static void parse_navpoint(TOCList *toc, xmlNodePtr np, int depth, Book *book) {
    const char *label_text = NULL;
    const char *content_src = NULL;
    char *label_buf = NULL;
    char *src_buf = NULL;

    for (xmlNodePtr n = np->children; n; n = n->next) {
        if (n->type != XML_ELEMENT_NODE) continue;

        if (strcmp((char *)n->name, "navLabel") == 0) {
            xmlNodePtr text_node = NULL;
            for (xmlNodePtr c = n->children; c; c = c->next) {
                if (c->type == XML_ELEMENT_NODE && strcmp((char *)c->name, "text") == 0) {
                    text_node = c;
                    break;
                }
            }
            if (text_node) {
                label_buf = get_text_content(text_node);
                label_text = label_buf;
            }
        } else if (strcmp((char *)n->name, "content") == 0) {
            xmlChar *src = xmlGetProp(n, (xmlChar *)"src");
            if (src) {
                src_buf = xstrdup((char *)src);
                content_src = src_buf;
                xmlFree(src);
            }
        }
    }

    if (label_text && content_src)
        toc_add(toc, label_text, content_src, depth, book);

    free(label_buf);
    free(src_buf);

    /* Recurse into child navPoints */
    for (xmlNodePtr n = np->children; n; n = n->next) {
        if (n->type == XML_ELEMENT_NODE && strcmp((char *)n->name, "navPoint") == 0)
            parse_navpoint(toc, n, depth + 1, book);
    }
}

static void parse_ncx(TOCList *toc, const char *xml, size_t len, Book *book) {
    xmlDocPtr doc = xmlReadMemory(xml, (int)len, NULL, NULL,
                                  XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) return;

    xmlNodePtr root = xmlDocGetRootElement(doc);
    /* Find navMap */
    xmlNodePtr navMap = NULL;
    for (xmlNodePtr n = root->children; n; n = n->next) {
        if (n->type == XML_ELEMENT_NODE && strcmp((char *)n->name, "navMap") == 0) {
            navMap = n;
            break;
        }
    }

    if (navMap) {
        for (xmlNodePtr n = navMap->children; n; n = n->next) {
            if (n->type == XML_ELEMENT_NODE && strcmp((char *)n->name, "navPoint") == 0)
                parse_navpoint(toc, n, 0, book);
        }
    }

    xmlFreeDoc(doc);
}

/* ---- EPUB 3: parse nav.xhtml ---- */

static void find_nav_ol(xmlNodePtr node, xmlNodePtr *result) {
    if (*result) return;
    if (node->type == XML_ELEMENT_NODE) {
        if (strcmp((char *)node->name, "nav") == 0) {
            /* Check for epub:type="toc" or role="doc-toc" or just first nav */
            xmlChar *etype = xmlGetProp(node, (xmlChar *)"type");
            xmlChar *role = xmlGetProp(node, (xmlChar *)"role");
            int is_toc = 0;
            if (etype && strstr((char *)etype, "toc")) is_toc = 1;
            if (role && strstr((char *)role, "doc-toc")) is_toc = 1;
            if (!etype && !role) is_toc = 1; /* fallback: first nav */
            if (etype) xmlFree(etype);
            if (role) xmlFree(role);

            if (is_toc) {
                /* Find <ol> inside */
                for (xmlNodePtr c = node->children; c; c = c->next) {
                    if (c->type == XML_ELEMENT_NODE && strcmp((char *)c->name, "ol") == 0) {
                        *result = c;
                        return;
                    }
                }
            }
        }
    }
    for (xmlNodePtr c = node->children; c; c = c->next)
        find_nav_ol(c, result);
}

static void parse_nav_ol(TOCList *toc, xmlNodePtr ol, int depth, Book *book) {
    for (xmlNodePtr li = ol->children; li; li = li->next) {
        if (li->type != XML_ELEMENT_NODE || strcmp((char *)li->name, "li") != 0)
            continue;

        for (xmlNodePtr child = li->children; child; child = child->next) {
            if (child->type != XML_ELEMENT_NODE) continue;

            if (strcmp((char *)child->name, "a") == 0) {
                xmlChar *href = xmlGetProp(child, (xmlChar *)"href");
                char *label = get_text_content(child);
                if (href && label[0])
                    toc_add(toc, label, (char *)href, depth, book);
                if (href) xmlFree(href);
                free(label);
            } else if (strcmp((char *)child->name, "ol") == 0) {
                parse_nav_ol(toc, child, depth + 1, book);
            }
        }
    }
}

static void parse_nav_xhtml(TOCList *toc, const char *html, size_t len, Book *book) {
    htmlDocPtr doc = htmlReadMemory(html, (int)len, NULL, "UTF-8",
                                    HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING |
                                    HTML_PARSE_RECOVER);
    if (!doc) return;

    xmlNodePtr root = xmlDocGetRootElement(doc);
    xmlNodePtr ol = NULL;
    if (root) find_nav_ol(root, &ol);

    if (ol) parse_nav_ol(toc, ol, 0, book);

    xmlFreeDoc(doc);
}

void toc_parse(TOCList *toc, Book *book) {
    memset(toc, 0, sizeof(*toc));
    if (!book->toc_href) return;

    /* Determine if NCX or nav.xhtml based on extension */
    int is_ncx = str_endswith(book->toc_href, ".ncx");

    /* Read the TOC file - toc_href is already an absolute path within the ZIP */
    char *toc_dir = path_dirname(book->toc_href);
    size_t data_len;
    char *data = NULL;

    /* toc_href is already resolved with opf_dir, but epub_read_file prepends opf_dir again.
       So we need the path relative to opf_dir. */
    if (book->opf_dir[0]) {
        size_t prefix_len = strlen(book->opf_dir);
        const char *rel = book->toc_href;
        if (strncmp(rel, book->opf_dir, prefix_len) == 0) {
            rel += prefix_len;
            if (*rel == '/') rel++;
        }
        data = epub_read_file(book, rel, &data_len);
    } else {
        data = epub_read_file(book, book->toc_href, &data_len);
    }

    free(toc_dir);

    if (!data) return;

    if (is_ncx)
        parse_ncx(toc, data, data_len, book);
    else
        parse_nav_xhtml(toc, data, data_len, book);

    free(data);
}

void toc_free(TOCList *toc) {
    for (int i = 0; i < toc->count; i++) {
        free(toc->entries[i].label);
        free(toc->entries[i].href);
    }
    free(toc->entries);
    memset(toc, 0, sizeof(*toc));
}
