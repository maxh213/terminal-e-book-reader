#include "renderer.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>
#include <libxml/HTMLparser.h>
#include <libxml/tree.h>

/* Internal state for the render walk */
typedef struct {
    RenderResult *rr;
    int width;
    /* Current line buffer */
    char *buf;
    unsigned char *abuf;
    int buf_len;
    int buf_cap;
    int col;             /* visual column position */
    /* Attribute stack */
    unsigned char cur_attr;
    /* Link tracking */
    int in_link;
    int cur_link_idx;
    char *cur_href;
    /* Flags */
    int in_pre;
    int last_was_block;
    int need_indent;
    int heading_level;
} RenderCtx;

static void ctx_init(RenderCtx *ctx, RenderResult *rr, int width) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->rr = rr;
    ctx->width = width;
    ctx->buf_cap = 256;
    ctx->buf = xmalloc((size_t)ctx->buf_cap);
    ctx->abuf = xmalloc((size_t)ctx->buf_cap);
    ctx->buf[0] = '\0';
    ctx->last_was_block = 1;
}

static void ensure_buf(RenderCtx *ctx, int extra) {
    while (ctx->buf_len + extra + 1 > ctx->buf_cap) {
        ctx->buf_cap *= 2;
        ctx->buf = xrealloc(ctx->buf, (size_t)ctx->buf_cap);
        ctx->abuf = xrealloc(ctx->abuf, (size_t)ctx->buf_cap);
    }
}

static void emit_line(RenderCtx *ctx) {
    RenderResult *rr = ctx->rr;
    if (rr->line_count >= rr->line_cap) {
        rr->line_cap = rr->line_cap ? rr->line_cap * 2 : 64;
        rr->lines = xrealloc(rr->lines, (size_t)rr->line_cap * sizeof(RenderLine));
    }

    RenderLine *rl = &rr->lines[rr->line_count++];
    rl->text = xstrndup(ctx->buf, (size_t)ctx->buf_len);
    rl->attrs = xmalloc((size_t)ctx->buf_len + 1);
    memcpy(rl->attrs, ctx->abuf, (size_t)ctx->buf_len);
    rl->attrs[ctx->buf_len] = 0;
    rl->len = ctx->buf_len;

    ctx->buf_len = 0;
    ctx->buf[0] = '\0';
    ctx->col = 0;
}

static void add_anchor(RenderCtx *ctx, const char *id) {
    if (!id) return;
    RenderResult *rr = ctx->rr;
    if (rr->anchor_count >= rr->anchor_cap) {
        rr->anchor_cap = rr->anchor_cap ? rr->anchor_cap * 2 : 16;
        rr->anchors = xrealloc(rr->anchors, (size_t)rr->anchor_cap * sizeof(Anchor));
    }
    Anchor *a = &rr->anchors[rr->anchor_count++];
    a->id = xstrdup(id);
    a->line = rr->line_count; /* current line being built */
}

static void begin_link(RenderCtx *ctx, const char *href) {
    if (!href) return;
    RenderResult *rr = ctx->rr;
    if (rr->link_count >= rr->link_cap) {
        rr->link_cap = rr->link_cap ? rr->link_cap * 2 : 16;
        rr->links = xrealloc(rr->links, (size_t)rr->link_cap * sizeof(Link));
    }
    Link *lk = &rr->links[rr->link_count];
    memset(lk, 0, sizeof(*lk));
    lk->href = xstrdup(href);
    lk->line_start = rr->line_count;
    lk->col_start = ctx->buf_len;
    lk->id = rr->link_count;

    ctx->in_link = 1;
    ctx->cur_link_idx = rr->link_count;
    ctx->cur_href = lk->href;
    ctx->cur_attr |= ATTR_LINK;
    rr->link_count++;
}

static void end_link(RenderCtx *ctx) {
    if (!ctx->in_link) return;
    RenderResult *rr = ctx->rr;
    Link *lk = &rr->links[ctx->cur_link_idx];
    lk->line_end = rr->line_count;
    lk->col_end = ctx->buf_len;
    ctx->in_link = 0;
    ctx->cur_href = NULL;
    ctx->cur_attr &= ~ATTR_LINK;
}

/* Get visual width of a UTF-8 character */
static int utf8_char_width(const char *s, int *byte_len) {
    unsigned char c = (unsigned char)s[0];
    wchar_t wc;
    int blen;

    if (c < 0x80) {
        *byte_len = 1;
        return (c < 0x20) ? 0 : 1;
    } else if (c < 0xC0) {
        *byte_len = 1;
        return 0; /* continuation byte */
    } else if (c < 0xE0) {
        blen = 2;
    } else if (c < 0xF0) {
        blen = 3;
    } else {
        blen = 4;
    }

    /* Decode to wchar for wcwidth */
    mbstate_t mbs;
    memset(&mbs, 0, sizeof(mbs));
    size_t rc = mbrtowc(&wc, s, (size_t)blen, &mbs);
    if (rc == (size_t)-1 || rc == (size_t)-2) {
        *byte_len = 1;
        return 1;
    }
    *byte_len = (int)rc;
    int w = wcwidth(wc);
    return (w < 0) ? 0 : w;
}

static void append_char(RenderCtx *ctx, const char *ch, int byte_len, int vis_width) {
    /* Word wrap: if adding this char would exceed width, emit line */
    if (ctx->col + vis_width > ctx->width && ctx->col > 0) {
        /* Try to break at last space */
        int break_pos = -1;
        for (int i = ctx->buf_len - 1; i > 0; i--) {
            if (ctx->buf[i] == ' ') {
                break_pos = i;
                break;
            }
        }

        if (break_pos > 0) {
            /* Save text after break */
            int rest_len = ctx->buf_len - break_pos - 1;
            char *rest = NULL;
            unsigned char *rattr = NULL;
            if (rest_len > 0) {
                rest = xstrndup(ctx->buf + break_pos + 1, (size_t)rest_len);
                rattr = xmalloc((size_t)rest_len);
                memcpy(rattr, ctx->abuf + break_pos + 1, (size_t)rest_len);
            }

            ctx->buf_len = break_pos;
            ctx->buf[break_pos] = '\0';
            emit_line(ctx);

            /* Restore rest */
            if (rest_len > 0 && rest) {
                ensure_buf(ctx, rest_len);
                memcpy(ctx->buf, rest, (size_t)rest_len);
                memcpy(ctx->abuf, rattr, (size_t)rest_len);
                ctx->buf_len = rest_len;
                ctx->buf[rest_len] = '\0';
                /* Recalculate column */
                ctx->col = 0;
                for (int i = 0; i < rest_len; ) {
                    int bl, vw;
                    vw = utf8_char_width(ctx->buf + i, &bl);
                    ctx->col += vw;
                    i += bl;
                }
            }
            free(rest);
            free(rattr);
        } else {
            emit_line(ctx);
        }
    }

    ensure_buf(ctx, byte_len);
    memcpy(ctx->buf + ctx->buf_len, ch, (size_t)byte_len);
    for (int i = 0; i < byte_len; i++)
        ctx->abuf[ctx->buf_len + i] = ctx->cur_attr;
    ctx->buf_len += byte_len;
    ctx->buf[ctx->buf_len] = '\0';
    ctx->col += vis_width;
}

static void append_text(RenderCtx *ctx, const char *text) {
    if (!text) return;
    const char *p = text;
    while (*p) {
        if (*p == '\n' || *p == '\r') {
            if (ctx->in_pre) {
                emit_line(ctx);
            }
            p++;
            if (*(p - 1) == '\r' && *p == '\n') p++;
            continue;
        }

        if (!ctx->in_pre && isspace((unsigned char)*p)) {
            /* Collapse whitespace */
            while (*p && isspace((unsigned char)*p)) p++;
            if (ctx->buf_len > 0 && ctx->buf[ctx->buf_len - 1] != ' ') {
                append_char(ctx, " ", 1, 1);
            }
            continue;
        }

        int byte_len, vis_width;
        vis_width = utf8_char_width(p, &byte_len);
        append_char(ctx, p, byte_len, vis_width);
        p += byte_len;
    }
}

static int is_block_element(const char *name) {
    static const char *blocks[] = {
        "p", "div", "h1", "h2", "h3", "h4", "h5", "h6",
        "blockquote", "pre", "ul", "ol", "li", "table",
        "tr", "section", "article", "header", "footer",
        "aside", "nav", "figure", "figcaption", "br",
        "hr", "dt", "dd", "dl", NULL
    };
    for (int i = 0; blocks[i]; i++)
        if (strcmp(name, blocks[i]) == 0) return 1;
    return 0;
}

static void handle_block_start(RenderCtx *ctx) {
    if (ctx->buf_len > 0)
        emit_line(ctx);
    if (!ctx->last_was_block && ctx->rr->line_count > 0) {
        /* Add blank line between blocks */
    }
    ctx->last_was_block = 1;
}

static void walk_node(RenderCtx *ctx, xmlNodePtr node);

static void walk_children(RenderCtx *ctx, xmlNodePtr node) {
    for (xmlNodePtr child = node->children; child; child = child->next)
        walk_node(ctx, child);
}

static void walk_node(RenderCtx *ctx, xmlNodePtr node) {
    if (node->type == XML_TEXT_NODE) {
        append_text(ctx, (char *)node->content);
        ctx->last_was_block = 0;
        return;
    }

    if (node->type != XML_ELEMENT_NODE) {
        walk_children(ctx, node);
        return;
    }

    const char *name = (char *)node->name;

    /* Check for id/name anchors */
    char *id = NULL;
    xmlChar *xid = xmlGetProp(node, (xmlChar *)"id");
    if (xid) { id = xstrdup((char *)xid); xmlFree(xid); }
    if (!id) {
        xmlChar *xname = xmlGetProp(node, (xmlChar *)"name");
        if (xname) { id = xstrdup((char *)xname); xmlFree(xname); }
    }
    if (id) {
        add_anchor(ctx, id);
        free(id);
    }

    /* Skip non-visible elements */
    if (strcmp(name, "script") == 0 || strcmp(name, "style") == 0 ||
        strcmp(name, "head") == 0)
        return;

    /* Handle specific elements */
    int is_block = is_block_element(name);
    if (is_block) handle_block_start(ctx);

    unsigned char saved_attr = ctx->cur_attr;
    int saved_heading = ctx->heading_level;

    if (strcmp(name, "b") == 0 || strcmp(name, "strong") == 0) {
        ctx->cur_attr |= ATTR_BOLD;
    } else if (strcmp(name, "i") == 0 || strcmp(name, "em") == 0 ||
               strcmp(name, "cite") == 0) {
        ctx->cur_attr |= ATTR_ITALIC;
    } else if (strcmp(name, "u") == 0) {
        ctx->cur_attr |= ATTR_UNDERLINE;
    } else if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2]) {
        ctx->cur_attr |= ATTR_HEADING | ATTR_BOLD;
        ctx->heading_level = name[1] - '0';
    } else if (strcmp(name, "a") == 0) {
        xmlChar *href = xmlGetProp(node, (xmlChar *)"href");
        if (href) {
            begin_link(ctx, (char *)href);
            xmlFree(href);
        }
    } else if (strcmp(name, "pre") == 0 || strcmp(name, "code") == 0) {
        ctx->in_pre++;
    } else if (strcmp(name, "br") == 0) {
        emit_line(ctx);
        walk_children(ctx, node);
        ctx->cur_attr = saved_attr;
        ctx->heading_level = saved_heading;
        return;
    } else if (strcmp(name, "hr") == 0) {
        /* Draw a horizontal rule */
        int rule_width = ctx->width < 40 ? ctx->width : 40;
        for (int i = 0; i < rule_width; i++)
            append_char(ctx, "\xe2\x94\x80", 3, 1); /* U+2500 BOX DRAWINGS LIGHT HORIZONTAL */
        emit_line(ctx);
        walk_children(ctx, node);
        ctx->cur_attr = saved_attr;
        ctx->heading_level = saved_heading;
        return;
    } else if (strcmp(name, "li") == 0) {
        append_text(ctx, "  \xe2\x80\xa2 "); /* U+2022 BULLET */
    } else if (strcmp(name, "img") == 0) {
        xmlChar *alt = xmlGetProp(node, (xmlChar *)"alt");
        if (alt && ((char *)alt)[0]) {
            append_char(ctx, "[", 1, 1);
            append_text(ctx, (char *)alt);
            append_char(ctx, "]", 1, 1);
        }
        if (alt) xmlFree(alt);
    }

    walk_children(ctx, node);

    /* Restore state */
    if (strcmp(name, "a") == 0) end_link(ctx);
    if (strcmp(name, "pre") == 0 || strcmp(name, "code") == 0) ctx->in_pre--;
    ctx->cur_attr = saved_attr;
    ctx->heading_level = saved_heading;

    if (is_block) {
        if (ctx->buf_len > 0) emit_line(ctx);
        ctx->last_was_block = 1;
    }
}

void render_xhtml(RenderResult *rr, const char *xhtml, size_t len, int width) {
    memset(rr, 0, sizeof(*rr));
    if (!xhtml || len == 0 || width <= 0) return;

    /* Use HTML parser for tolerance */
    htmlDocPtr doc = htmlReadMemory(xhtml, (int)len, NULL, "UTF-8",
                                    HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING |
                                    HTML_PARSE_RECOVER | HTML_PARSE_NONET);
    if (!doc) return;

    RenderCtx ctx;
    ctx_init(&ctx, rr, width);

    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (root) {
        /* Look for <body> */
        xmlNodePtr body = NULL;
        for (xmlNodePtr n = root->children; n; n = n->next) {
            if (n->type == XML_ELEMENT_NODE && strcmp((char *)n->name, "body") == 0) {
                body = n;
                break;
            }
        }
        walk_node(&ctx, body ? body : root);
    }

    /* Flush remaining buffer */
    if (ctx.buf_len > 0) emit_line(&ctx);

    free(ctx.buf);
    free(ctx.abuf);
    xmlFreeDoc(doc);
}

int render_find_anchor(RenderResult *rr, const char *id) {
    if (!id || !rr) return -1;
    for (int i = 0; i < rr->anchor_count; i++) {
        if (strcmp(rr->anchors[i].id, id) == 0)
            return rr->anchors[i].line;
    }
    return -1;
}

void render_free(RenderResult *rr) {
    for (int i = 0; i < rr->line_count; i++) {
        free(rr->lines[i].text);
        free(rr->lines[i].attrs);
    }
    free(rr->lines);
    for (int i = 0; i < rr->link_count; i++)
        free(rr->links[i].href);
    free(rr->links);
    for (int i = 0; i < rr->anchor_count; i++)
        free(rr->anchors[i].id);
    free(rr->anchors);
    memset(rr, 0, sizeof(*rr));
}
