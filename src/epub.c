#include "epub.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zip.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

static char *zip_read_file(zip_t *za, const char *name, size_t *out_len) {
    zip_stat_t st;
    if (zip_stat(za, name, 0, &st) != 0) return NULL;

    zip_file_t *zf = zip_fopen(za, name, 0);
    if (!zf) return NULL;

    size_t len = (size_t)st.size;
    char *buf = xmalloc(len + 1);
    zip_int64_t nread = zip_fread(zf, buf, len);
    zip_fclose(zf);

    if (nread < 0 || (size_t)nread != len) { free(buf); return NULL; }
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

static char *find_opf_path(zip_t *za) {
    size_t len;
    char *xml = zip_read_file(za, "META-INF/container.xml", &len);
    if (!xml) return NULL;

    xmlDocPtr doc = xmlReadMemory(xml, (int)len, NULL, NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    free(xml);
    if (!doc) return NULL;

    char *result = NULL;
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
    xmlXPathRegisterNs(ctx, (xmlChar *)"c", (xmlChar *)"urn:oasis:names:tc:opendocument:xmlns:container");
    xmlXPathObjectPtr obj = xmlXPathEvalExpression(
        (xmlChar *)"//c:rootfile/@full-path", ctx);

    if (obj && obj->nodesetval && obj->nodesetval->nodeNr > 0) {
        xmlChar *val = xmlNodeGetContent(obj->nodesetval->nodeTab[0]);
        if (val) {
            result = xstrdup((char *)val);
            xmlFree(val);
        }
    }

    if (obj) xmlXPathFreeObject(obj);
    xmlXPathFreeContext(ctx);
    xmlFreeDoc(doc);
    return result;
}

static xmlNodePtr find_child(xmlNodePtr parent, const char *name) {
    for (xmlNodePtr n = parent->children; n; n = n->next) {
        if (n->type == XML_ELEMENT_NODE && strcmp((char *)n->name, name) == 0)
            return n;
    }
    return NULL;
}

static char *get_attr(xmlNodePtr node, const char *name) {
    xmlChar *val = xmlGetProp(node, (xmlChar *)name);
    if (!val) return NULL;
    char *s = xstrdup((char *)val);
    xmlFree(val);
    return s;
}

static int parse_opf(Book *book, const char *opf_xml, size_t opf_len) {
    xmlDocPtr doc = xmlReadMemory(opf_xml, (int)opf_len, NULL, NULL,
                                  XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) return -1;

    xmlNodePtr root = xmlDocGetRootElement(doc);

    /* Detect EPUB version */
    char *ver = get_attr(root, "version");
    if (ver) {
        book->version = (ver[0] == '3') ? 3 : 2;
        free(ver);
    } else {
        book->version = 2;
    }

    /* Find metadata, manifest, spine */
    xmlNodePtr metadata = find_child(root, "metadata");
    xmlNodePtr manifest = find_child(root, "manifest");
    xmlNodePtr spine = find_child(root, "spine");

    /* Parse metadata */
    if (metadata) {
        for (xmlNodePtr n = metadata->children; n; n = n->next) {
            if (n->type != XML_ELEMENT_NODE) continue;
            if (strcmp((char *)n->name, "title") == 0 && !book->title) {
                xmlChar *t = xmlNodeGetContent(n);
                if (t) { book->title = xstrdup((char *)t); xmlFree(t); }
            } else if (strcmp((char *)n->name, "creator") == 0 && !book->author) {
                xmlChar *t = xmlNodeGetContent(n);
                if (t) { book->author = xstrdup((char *)t); xmlFree(t); }
            }
        }
    }

    /* Parse manifest */
    if (manifest) {
        /* Count items */
        int count = 0;
        for (xmlNodePtr n = manifest->children; n; n = n->next)
            if (n->type == XML_ELEMENT_NODE && strcmp((char *)n->name, "item") == 0)
                count++;

        book->manifest = xcalloc((size_t)count, sizeof(ManifestItem));
        book->manifest_count = 0;

        for (xmlNodePtr n = manifest->children; n; n = n->next) {
            if (n->type != XML_ELEMENT_NODE || strcmp((char *)n->name, "item") != 0)
                continue;
            ManifestItem *item = &book->manifest[book->manifest_count++];
            item->id = get_attr(n, "id");
            item->href = get_attr(n, "href");
            item->media_type = get_attr(n, "media-type");

            /* EPUB 3: detect nav document */
            char *props = get_attr(n, "properties");
            if (props) {
                if (strstr(props, "nav") && item->href) {
                    free(book->toc_href);
                    book->toc_href = path_join(book->opf_dir, item->href);
                }
                free(props);
            }
        }
    }

    /* Parse spine */
    if (spine) {
        /* EPUB 2: get toc NCX reference */
        char *toc_id = get_attr(spine, "toc");
        if (toc_id && !book->toc_href) {
            for (int i = 0; i < book->manifest_count; i++) {
                if (book->manifest[i].id && strcmp(book->manifest[i].id, toc_id) == 0) {
                    book->toc_href = path_join(book->opf_dir, book->manifest[i].href);
                    break;
                }
            }
        }
        free(toc_id);

        /* Count spine items */
        int count = 0;
        for (xmlNodePtr n = spine->children; n; n = n->next)
            if (n->type == XML_ELEMENT_NODE && strcmp((char *)n->name, "itemref") == 0)
                count++;

        book->spine = xcalloc((size_t)count, sizeof(int));
        book->spine_count = 0;

        for (xmlNodePtr n = spine->children; n; n = n->next) {
            if (n->type != XML_ELEMENT_NODE || strcmp((char *)n->name, "itemref") != 0)
                continue;
            char *idref = get_attr(n, "idref");
            if (!idref) continue;

            for (int i = 0; i < book->manifest_count; i++) {
                if (book->manifest[i].id && strcmp(book->manifest[i].id, idref) == 0) {
                    book->spine[book->spine_count++] = i;
                    break;
                }
            }
            free(idref);
        }
    }

    xmlFreeDoc(doc);
    return 0;
}

int epub_open(Book *book, const char *path) {
    memset(book, 0, sizeof(*book));

    int err;
    zip_t *za = zip_open(path, ZIP_RDONLY, &err);
    if (!za) {
        fprintf(stderr, "tread: cannot open ZIP: error %d\n", err);
        return -1;
    }
    book->zip_archive = za;

    char *opf_path = find_opf_path(za);
    if (!opf_path) {
        fprintf(stderr, "tread: cannot find OPF in container.xml\n");
        epub_close(book);
        return -1;
    }

    book->opf_dir = path_dirname(opf_path);
    if (strcmp(book->opf_dir, ".") == 0) {
        free(book->opf_dir);
        book->opf_dir = xstrdup("");
    }

    size_t opf_len;
    char *opf_xml = zip_read_file(za, opf_path, &opf_len);
    free(opf_path);

    if (!opf_xml) {
        fprintf(stderr, "tread: cannot read OPF file\n");
        epub_close(book);
        return -1;
    }

    int rc = parse_opf(book, opf_xml, opf_len);
    free(opf_xml);

    if (rc != 0) {
        fprintf(stderr, "tread: failed to parse OPF\n");
        epub_close(book);
        return -1;
    }

    if (book->spine_count == 0) {
        fprintf(stderr, "tread: no spine items found\n");
        epub_close(book);
        return -1;
    }

    if (!book->title) book->title = xstrdup("Untitled");
    if (!book->author) book->author = xstrdup("Unknown");

    return 0;
}

char *epub_read_file(Book *book, const char *href, size_t *len) {
    char *full = path_join(book->opf_dir, href);
    char *data = zip_read_file(book->zip_archive, full, len);
    free(full);
    return data;
}

const char *epub_spine_href(Book *book, int spine_index) {
    if (spine_index < 0 || spine_index >= book->spine_count) return NULL;
    return book->manifest[book->spine[spine_index]].href;
}

int epub_find_spine_index(Book *book, const char *href) {
    if (!href) return -1;
    char *clean = href_strip_fragment(href);

    for (int i = 0; i < book->spine_count; i++) {
        const char *spine_href = book->manifest[book->spine[i]].href;
        if (spine_href && strcmp(spine_href, clean) == 0) {
            free(clean);
            return i;
        }
    }

    /* Try with opf_dir prefix stripped */
    if (book->opf_dir[0]) {
        char *prefix = path_join(book->opf_dir, "");
        size_t plen = strlen(prefix);
        if (strncmp(clean, prefix, plen) == 0) {
            const char *rel = clean + plen;
            for (int i = 0; i < book->spine_count; i++) {
                const char *spine_href = book->manifest[book->spine[i]].href;
                if (spine_href && strcmp(spine_href, rel) == 0) {
                    free(prefix);
                    free(clean);
                    return i;
                }
            }
        }
        free(prefix);
    }

    free(clean);
    return -1;
}

void epub_close(Book *book) {
    if (book->zip_archive) {
        zip_close(book->zip_archive);
        book->zip_archive = NULL;
    }
    free(book->title);
    free(book->author);
    free(book->opf_dir);
    free(book->toc_href);
    for (int i = 0; i < book->manifest_count; i++) {
        free(book->manifest[i].id);
        free(book->manifest[i].href);
        free(book->manifest[i].media_type);
    }
    free(book->manifest);
    free(book->spine);
    memset(book, 0, sizeof(*book));
}
