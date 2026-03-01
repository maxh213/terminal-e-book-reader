#include "ui.h"
#include "renderer.h"
#include "theme.h"
#include "util.h"
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>

/* Abstract actions */
typedef enum {
    ACTION_NONE,
    ACTION_QUIT,
    ACTION_SCROLL_DOWN,
    ACTION_SCROLL_UP,
    ACTION_PAGE_DOWN,
    ACTION_PAGE_UP,
    ACTION_HALF_PAGE_DOWN,
    ACTION_HALF_PAGE_UP,
    ACTION_NEXT_CHAPTER,
    ACTION_PREV_CHAPTER,
    ACTION_TOC,
    ACTION_SEARCH,
    ACTION_SEARCH_BACK,
    ACTION_SEARCH_NEXT,
    ACTION_SEARCH_PREV,
    ACTION_CYCLE_LINK_FWD,
    ACTION_CYCLE_LINK_BACK,
    ACTION_FOLLOW_LINK,
    ACTION_GO_BACK,
    ACTION_THEME_SELECT,
    ACTION_HELP,
} Action;

/* Back stack for link navigation */
#define BACK_STACK_SIZE 32
typedef struct {
    int spine_index;
    int scroll_line;
} BackEntry;

/* UI state */
typedef struct {
    Book *book;
    TOCList *toc;
    ReadingState *rs;
    Config *cfg;

    RenderResult rr;
    int scroll;          /* top visible line */
    int rows, cols;      /* terminal dimensions */
    int content_rows;    /* rows - 1 (status bar) */

    /* Link selection */
    int selected_link;   /* -1 = no link selected */

    /* Back stack */
    BackEntry back_stack[BACK_STACK_SIZE];
    int back_top;
    int back_count;

    /* Search */
    char search_query[256];
    int search_dir;       /* 1 = forward, -1 = backward */
    int *search_hits;     /* line numbers with matches */
    int search_hit_count;
    int search_hit_cap;
    int search_current;   /* index into search_hits */

    int running;
} UIState;

static volatile sig_atomic_t got_resize = 0;

static void handle_resize(int sig) {
    (void)sig;
    got_resize = 1;
}

/* ---- Chapter loading ---- */

static void load_chapter(UIState *ui) {
    render_free(&ui->rr);
    ui->selected_link = -1;

    const char *href = epub_spine_href(ui->book, ui->rs->spine_index);
    if (!href) return;

    size_t len;
    char *xhtml = epub_read_file(ui->book, href, &len);
    if (!xhtml) return;

    render_xhtml(&ui->rr, xhtml, len, ui->cols - 2); /* 1 char margin each side */
    free(xhtml);
}

/* ---- Drawing ---- */

static void draw_line(UIState *ui, int screen_row, RenderLine *rl) {
    move(screen_row, 1); /* 1 char left margin */
    clrtoeol();

    if (!rl || !rl->text) return;

    int col = 1;
    int max_col = ui->cols - 1;
    unsigned char prev_attr = 0;

    for (int i = 0; i < rl->len && col < max_col; ) {
        unsigned char attr = rl->attrs[i];

        if (attr != prev_attr) {
            attrset(A_NORMAL);
            if (attr & ATTR_HEADING)
                attron(COLOR_PAIR(PAIR_HEADING));
            else if (attr & ATTR_LINK)
                attron(COLOR_PAIR(PAIR_LINK));
            else
                attron(COLOR_PAIR(PAIR_TEXT));

            if (attr & ATTR_BOLD) attron(A_BOLD);
            if (attr & ATTR_ITALIC) attron(A_ITALIC);
            if (attr & ATTR_UNDERLINE) attron(A_UNDERLINE);
            if (attr & ATTR_HIGHLIGHT) {
                attron(COLOR_PAIR(PAIR_HIGHLIGHT));
            }
            prev_attr = attr;
        }

        /* Print one UTF-8 character */
        unsigned char c = (unsigned char)rl->text[i];
        int byte_len = 1;
        if (c >= 0xC0 && c < 0xE0) byte_len = 2;
        else if (c >= 0xE0 && c < 0xF0) byte_len = 3;
        else if (c >= 0xF0) byte_len = 4;

        if (i + byte_len <= rl->len) {
            char tmp[5] = {0};
            memcpy(tmp, rl->text + i, (size_t)byte_len);
            addstr(tmp);
        }
        i += byte_len;
        col++;
    }

    attrset(A_NORMAL);
    attron(COLOR_PAIR(PAIR_TEXT));
}

static void draw_status_bar(UIState *ui) {
    attron(COLOR_PAIR(PAIR_STATUS));
    move(ui->rows - 1, 0);
    clrtoeol();

    /* Left: title and author */
    char left[256];
    snprintf(left, sizeof(left), " %s - %s", ui->book->title, ui->book->author);

    /* Right: chapter/total, percentage */
    char right[128];
    int pct = 0;
    if (ui->rr.line_count > 0) {
        int visible_end = ui->scroll + ui->content_rows;
        if (visible_end > ui->rr.line_count) visible_end = ui->rr.line_count;
        pct = visible_end * 100 / ui->rr.line_count;
    }
    snprintf(right, sizeof(right), "Ch %d/%d  %d%% ",
             ui->rs->spine_index + 1, ui->book->spine_count, pct);

    mvaddnstr(ui->rows - 1, 0, left, ui->cols - (int)strlen(right) - 1);
    mvaddstr(ui->rows - 1, ui->cols - (int)strlen(right), right);

    attrset(A_NORMAL);
    attron(COLOR_PAIR(PAIR_TEXT));
}

static void highlight_selected_link(UIState *ui) {
    if (ui->selected_link < 0 || ui->selected_link >= ui->rr.link_count)
        return;
    Link *lk = &ui->rr.links[ui->selected_link];
    for (int line = lk->line_start; line <= lk->line_end; line++) {
        int screen_row = line - ui->scroll;
        if (screen_row < 0 || screen_row >= ui->content_rows) continue;
        RenderLine *rl = &ui->rr.lines[line];

        int col_start = (line == lk->line_start) ? lk->col_start : 0;
        int col_end = (line == lk->line_end) ? lk->col_end : rl->len;

        move(screen_row, 1);
        attron(COLOR_PAIR(PAIR_LINK) | A_REVERSE);

        for (int i = 0; i < rl->len; ) {
            unsigned char c = (unsigned char)rl->text[i];
            int byte_len = 1;
            if (c >= 0xC0 && c < 0xE0) byte_len = 2;
            else if (c >= 0xE0 && c < 0xF0) byte_len = 3;
            else if (c >= 0xF0) byte_len = 4;

            if (i >= col_start && i < col_end) {
                char tmp[5] = {0};
                memcpy(tmp, rl->text + i, (size_t)(i + byte_len <= rl->len ? byte_len : rl->len - i));
                mvaddstr(screen_row, 1 + i, tmp);
            }
            i += byte_len;
        }

        attrset(A_NORMAL);
        attron(COLOR_PAIR(PAIR_TEXT));
    }
}

static void draw_screen(UIState *ui) {
    erase();
    attron(COLOR_PAIR(PAIR_TEXT));

    for (int i = 0; i < ui->content_rows; i++) {
        int line_idx = ui->scroll + i;
        if (line_idx < ui->rr.line_count)
            draw_line(ui, i, &ui->rr.lines[line_idx]);
        else {
            move(i, 0);
            clrtoeol();
        }
    }

    /* Highlight search matches on visible lines */
    if (ui->search_query[0]) {
        size_t qlen = strlen(ui->search_query);
        for (int i = 0; i < ui->content_rows; i++) {
            int line_idx = ui->scroll + i;
            if (line_idx >= ui->rr.line_count) break;
            RenderLine *rl = &ui->rr.lines[line_idx];
            char *pos = rl->text;
            while ((pos = strcasestr(pos, ui->search_query)) != NULL) {
                int offset = (int)(pos - rl->text);
                attron(COLOR_PAIR(PAIR_HIGHLIGHT));
                char tmp[256];
                int copy_len = (int)qlen;
                if (copy_len > (int)sizeof(tmp) - 1) copy_len = (int)sizeof(tmp) - 1;
                memcpy(tmp, pos, (size_t)copy_len);
                tmp[copy_len] = '\0';
                mvaddstr(i, 1 + offset, tmp);
                attrset(A_NORMAL);
                attron(COLOR_PAIR(PAIR_TEXT));
                pos += qlen;
            }
        }
    }

    highlight_selected_link(ui);
    draw_status_bar(ui);
    refresh();
}

/* ---- Input mapping ---- */

static Action map_nano_key(int ch) {
    switch (ch) {
        case KEY_DOWN:
        case '\n':
        case KEY_ENTER:
            return ACTION_SCROLL_DOWN;
        case KEY_UP:
            return ACTION_SCROLL_UP;
        case KEY_NPAGE:
        case ' ':
            return ACTION_PAGE_DOWN;
        case KEY_PPAGE:
        case KEY_BACKSPACE:
        case 127:
            return ACTION_PAGE_UP;
        case 14:  /* Ctrl+N */
            return ACTION_NEXT_CHAPTER;
        case 16:  /* Ctrl+P */
            return ACTION_PREV_CHAPTER;
        case 20:  /* Ctrl+T */
            return ACTION_TOC;
        case 23:  /* Ctrl+W */
            return ACTION_SEARCH;
        case '\t':
            return ACTION_CYCLE_LINK_FWD;
        case KEY_BTAB:
            return ACTION_CYCLE_LINK_BACK;
        case 2:   /* Ctrl+B */
            return ACTION_GO_BACK;
        case 5:   /* Ctrl+E */
            return ACTION_THEME_SELECT;
        case 24:  /* Ctrl+X */
            return ACTION_QUIT;
        case 7:   /* Ctrl+G */
            return ACTION_HELP;
        default:
            return ACTION_NONE;
    }
}

static Action map_vi_key(int ch, UIState *ui) {
    (void)ui;
    switch (ch) {
        case 'j':
        case KEY_DOWN:
            return ACTION_SCROLL_DOWN;
        case 'k':
        case KEY_UP:
            return ACTION_SCROLL_UP;
        case 6:   /* Ctrl+F */
        case ' ':
        case KEY_NPAGE:
            return ACTION_PAGE_DOWN;
        case 2:   /* Ctrl+B */
        case KEY_PPAGE:
            return ACTION_PAGE_UP;
        case 4:   /* Ctrl+D */
            return ACTION_HALF_PAGE_DOWN;
        case 21:  /* Ctrl+U */
            return ACTION_HALF_PAGE_UP;
        case 'l':
        case ']':
            return ACTION_NEXT_CHAPTER;
        case 'h':
        case '[':
            return ACTION_PREV_CHAPTER;
        case 't':
            return ACTION_TOC;
        case '/':
            return ACTION_SEARCH;
        case '?':
            return ACTION_SEARCH_BACK;
        case 'n':
            return ACTION_SEARCH_NEXT;
        case 'N':
            return ACTION_SEARCH_PREV;
        case '\t':
            return ACTION_CYCLE_LINK_FWD;
        case KEY_BTAB:
            return ACTION_CYCLE_LINK_BACK;
        case '\n':
        case KEY_ENTER:
            return ACTION_FOLLOW_LINK;
        case KEY_BACKSPACE:
        case 127:
            return ACTION_GO_BACK;
        case 'T':
            return ACTION_THEME_SELECT;
        case 'q':
            return ACTION_QUIT;
        default:
            return ACTION_NONE;
    }
}

/* For nano mode, Enter follows link if one is selected */
static Action map_key(UIState *ui, int ch) {
    Action a;
    if (ui->cfg->vi_mode)
        a = map_vi_key(ch, ui);
    else {
        a = map_nano_key(ch);
        /* In nano mode, Enter follows link when one is selected */
        if (a == ACTION_SCROLL_DOWN && (ch == '\n' || ch == KEY_ENTER) &&
            ui->selected_link >= 0)
            a = ACTION_FOLLOW_LINK;
    }
    return a;
}

/* ---- Navigation helpers ---- */

static void clamp_scroll(UIState *ui) {
    int max_scroll = ui->rr.line_count - ui->content_rows;
    if (max_scroll < 0) max_scroll = 0;
    if (ui->scroll > max_scroll) ui->scroll = max_scroll;
    if (ui->scroll < 0) ui->scroll = 0;
}

static void goto_chapter(UIState *ui, int spine_index) {
    if (spine_index < 0 || spine_index >= ui->book->spine_count) return;
    ui->rs->spine_index = spine_index;
    ui->scroll = 0;
    ui->selected_link = -1;
    load_chapter(ui);
}

static void push_back(UIState *ui) {
    BackEntry *e = &ui->back_stack[ui->back_top % BACK_STACK_SIZE];
    e->spine_index = ui->rs->spine_index;
    e->scroll_line = ui->scroll;
    ui->back_top = (ui->back_top + 1) % BACK_STACK_SIZE;
    if (ui->back_count < BACK_STACK_SIZE)
        ui->back_count++;
}

static int pop_back(UIState *ui, BackEntry *out) {
    if (ui->back_count <= 0) return -1;
    ui->back_top = (ui->back_top - 1 + BACK_STACK_SIZE) % BACK_STACK_SIZE;
    ui->back_count--;
    *out = ui->back_stack[ui->back_top];
    return 0;
}

/* ---- Search ---- */

static void build_search_hits(UIState *ui) {
    free(ui->search_hits);
    ui->search_hits = NULL;
    ui->search_hit_count = 0;
    ui->search_hit_cap = 0;
    ui->search_current = -1;

    if (!ui->search_query[0]) return;

    for (int i = 0; i < ui->rr.line_count; i++) {
        if (strcasestr(ui->rr.lines[i].text, ui->search_query)) {
            if (ui->search_hit_count >= ui->search_hit_cap) {
                ui->search_hit_cap = ui->search_hit_cap ? ui->search_hit_cap * 2 : 64;
                ui->search_hits = xrealloc(ui->search_hits,
                                           (size_t)ui->search_hit_cap * sizeof(int));
            }
            ui->search_hits[ui->search_hit_count++] = i;
        }
    }
}

static void search_jump(UIState *ui, int direction) {
    if (ui->search_hit_count == 0) return;

    if (direction > 0) {
        /* Find next hit after current scroll */
        ui->search_current = -1;
        for (int i = 0; i < ui->search_hit_count; i++) {
            if (ui->search_hits[i] > ui->scroll) {
                ui->search_current = i;
                break;
            }
        }
        if (ui->search_current < 0)
            ui->search_current = 0; /* wrap */
    } else {
        ui->search_current = -1;
        for (int i = ui->search_hit_count - 1; i >= 0; i--) {
            if (ui->search_hits[i] < ui->scroll) {
                ui->search_current = i;
                break;
            }
        }
        if (ui->search_current < 0)
            ui->search_current = ui->search_hit_count - 1; /* wrap */
    }

    if (ui->search_current >= 0)
        ui->scroll = ui->search_hits[ui->search_current];
    clamp_scroll(ui);
}

static void prompt_search(UIState *ui, int direction) {
    move(ui->rows - 1, 0);
    clrtoeol();
    attron(COLOR_PAIR(PAIR_STATUS));
    addstr(direction > 0 ? " Search: " : " Search (back): ");
    attrset(A_NORMAL);
    echo();
    curs_set(1);

    char buf[256] = {0};
    int pos = 0;
    int ch;

    while ((ch = getch()) != '\n' && ch != KEY_ENTER && ch != 27) {
        if (ch == KEY_BACKSPACE || ch == 127) {
            if (pos > 0) {
                pos--;
                buf[pos] = '\0';
                move(ui->rows - 1, direction > 0 ? 9 : 16);
                clrtoeol();
                addstr(buf);
            }
        } else if (ch >= 32 && ch < 127 && pos < (int)sizeof(buf) - 1) {
            buf[pos++] = (char)ch;
            buf[pos] = '\0';
        }
    }

    noecho();
    curs_set(0);

    if (ch == 27) return; /* Escaped */

    if (buf[0]) {
        strncpy(ui->search_query, buf, sizeof(ui->search_query) - 1);
        ui->search_dir = direction;
        build_search_hits(ui);
        search_jump(ui, direction);
    }
}

/* ---- Popup windows ---- */

static void draw_popup(const char *title, const char **items, int count,
                       int selected, int max_visible) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    int popup_w = cols * 2 / 3;
    if (popup_w < 30) popup_w = 30;
    if (popup_w > cols - 4) popup_w = cols - 4;

    int popup_h = count + 2;
    if (popup_h > max_visible + 2) popup_h = max_visible + 2;
    if (popup_h > rows - 4) popup_h = rows - 4;

    int start_y = (rows - popup_h) / 2;
    int start_x = (cols - popup_w) / 2;

    /* Draw border */
    attron(COLOR_PAIR(PAIR_TOC));
    for (int y = start_y; y < start_y + popup_h; y++) {
        move(y, start_x);
        for (int x = 0; x < popup_w; x++) addch(' ');
    }

    /* Title */
    int title_x = start_x + (popup_w - (int)strlen(title)) / 2;
    mvaddstr(start_y, title_x, title);

    /* Items with scrolling */
    int visible = popup_h - 2;
    int scroll_offset = 0;
    if (selected >= scroll_offset + visible)
        scroll_offset = selected - visible + 1;
    if (scroll_offset < 0) scroll_offset = 0;

    for (int i = 0; i < visible && i + scroll_offset < count; i++) {
        int idx = i + scroll_offset;
        if (idx == selected)
            attron(COLOR_PAIR(PAIR_TOC_SEL));
        else
            attron(COLOR_PAIR(PAIR_TOC));

        move(start_y + 1 + i, start_x + 1);
        int avail = popup_w - 2;
        char fmt[32];
        snprintf(fmt, sizeof(fmt), " %%-%d.%ds", avail - 1, avail - 1);
        printw(fmt, items[idx]);
    }

    attrset(A_NORMAL);
    refresh();
}

static int show_toc_popup(UIState *ui) {
    if (ui->toc->count == 0) return -1;

    int count = ui->toc->count;
    char **items = xmalloc((size_t)count * sizeof(char *));
    for (int i = 0; i < count; i++) {
        TOCEntry *e = &ui->toc->entries[i];
        int indent = e->depth * 2;
        size_t label_len = strlen(e->label);
        items[i] = xmalloc((size_t)indent + label_len + 1);
        memset(items[i], ' ', (size_t)indent);
        memcpy(items[i] + indent, e->label, label_len + 1);
    }

    /* Find current chapter in TOC */
    int selected = 0;
    for (int i = 0; i < count; i++) {
        if (ui->toc->entries[i].spine_index == ui->rs->spine_index) {
            selected = i;
            break;
        }
    }

    int result = -1;
    int ch;
    while (1) {
        draw_popup("Table of Contents", (const char **)items, count, selected,
                    ui->rows - 6);
        ch = getch();
        if (ch == 'q' || ch == 27 || ch == 24) break; /* q, Esc, Ctrl+X */
        if (ch == KEY_DOWN || ch == 'j') {
            if (selected < count - 1) selected++;
        } else if (ch == KEY_UP || ch == 'k') {
            if (selected > 0) selected--;
        } else if (ch == KEY_NPAGE) {
            selected += ui->content_rows;
            if (selected >= count) selected = count - 1;
        } else if (ch == KEY_PPAGE) {
            selected -= ui->content_rows;
            if (selected < 0) selected = 0;
        } else if (ch == '\n' || ch == KEY_ENTER) {
            result = selected;
            break;
        }
    }

    for (int i = 0; i < count; i++) free(items[i]);
    free(items);
    return result;
}

static int show_theme_popup(UIState *ui) {
    int count = theme_count();
    if (count == 0) return -1;

    const char **items = xmalloc((size_t)count * sizeof(char *));
    for (int i = 0; i < count; i++)
        items[i] = theme_name(i);

    int selected = 0;
    int result = -1;
    int ch;
    while (1) {
        draw_popup("Select Theme", items, count, selected, ui->rows - 6);
        ch = getch();
        if (ch == 'q' || ch == 27 || ch == 24) break;
        if (ch == KEY_DOWN || ch == 'j') {
            if (selected < count - 1) selected++;
        } else if (ch == KEY_UP || ch == 'k') {
            if (selected > 0) selected--;
        } else if (ch == '\n' || ch == KEY_ENTER) {
            result = selected;
            break;
        }
    }

    free(items);
    return result;
}

static void show_help(UIState *ui) {
    const char *nano_help[] = {
        "Navigation",
        "  Arrow Down/Up    Scroll down/up",
        "  PgDn/Space       Page down",
        "  PgUp/Backspace   Page up",
        "  Ctrl+N           Next chapter",
        "  Ctrl+P           Previous chapter",
        "",
        "Features",
        "  Ctrl+T           Open Table of Contents",
        "  Ctrl+W           Search",
        "  Tab/Shift+Tab    Cycle links",
        "  Enter            Follow link (when selected)",
        "  Ctrl+B           Go back",
        "  Ctrl+E           Theme selector",
        "  Ctrl+G           This help",
        "  Ctrl+X           Quit",
    };
    const char *vi_help[] = {
        "Navigation",
        "  j/k, arrows      Scroll down/up",
        "  Ctrl+F/Space      Page down",
        "  Ctrl+B/PgUp       Page up",
        "  Ctrl+D/Ctrl+U     Half page down/up",
        "  l/h, ]/[          Next/prev chapter",
        "",
        "Features",
        "  t                 Open Table of Contents",
        "  /                 Search forward",
        "  ?                 Search backward",
        "  n/N               Next/prev search match",
        "  Tab/Shift+Tab     Cycle links",
        "  Enter             Follow link",
        "  Backspace         Go back",
        "  T                 Theme selector",
        "  q                 Quit",
    };

    const char **help = ui->cfg->vi_mode ? vi_help : nano_help;
    int count = ui->cfg->vi_mode ?
        (int)(sizeof(vi_help) / sizeof(vi_help[0])) :
        (int)(sizeof(nano_help) / sizeof(nano_help[0]));

    int selected = 0;
    int ch;
    while (1) {
        draw_popup("Help - tread", help, count, -1, ui->rows - 6);
        ch = getch();
        if (ch == 'q' || ch == 27 || ch == 24 || ch == 7) break;
        (void)selected;
    }
}

/* ---- Link navigation ---- */

static void follow_link(UIState *ui) {
    if (ui->selected_link < 0 || ui->selected_link >= ui->rr.link_count)
        return;

    Link *lk = &ui->rr.links[ui->selected_link];
    if (!lk->href) return;

    /* Push current position to back stack */
    push_back(ui);

    /* Parse href - could be internal (#id), relative (chapter.xhtml#id), or same-chapter */
    const char *fragment = href_get_fragment(lk->href);
    char *file_part = href_strip_fragment(lk->href);

    if (!file_part || !file_part[0]) {
        /* Same-chapter jump to anchor */
        free(file_part);
        if (fragment) {
            int line = render_find_anchor(&ui->rr, fragment);
            if (line >= 0) {
                ui->scroll = line;
                clamp_scroll(ui);
            }
        }
        return;
    }

    /* Find in spine */
    int idx = epub_find_spine_index(ui->book, file_part);
    free(file_part);

    if (idx >= 0) {
        ui->rs->spine_index = idx;
        ui->scroll = 0;
        ui->selected_link = -1;
        load_chapter(ui);

        if (fragment) {
            int line = render_find_anchor(&ui->rr, fragment);
            if (line >= 0) {
                ui->scroll = line;
                clamp_scroll(ui);
            }
        }
    }
}

static void cycle_link(UIState *ui, int direction) {
    if (ui->rr.link_count == 0) return;

    /* Find links visible on screen */
    int first_visible = -1, last_visible = -1;
    for (int i = 0; i < ui->rr.link_count; i++) {
        Link *lk = &ui->rr.links[i];
        if (lk->line_start >= ui->scroll &&
            lk->line_start < ui->scroll + ui->content_rows) {
            if (first_visible < 0) first_visible = i;
            last_visible = i;
        }
    }

    if (first_visible < 0) {
        /* No visible links - find nearest */
        ui->selected_link = -1;
        return;
    }

    if (ui->selected_link < 0) {
        ui->selected_link = (direction > 0) ? first_visible : last_visible;
    } else {
        ui->selected_link += direction;
        if (ui->selected_link >= ui->rr.link_count) ui->selected_link = 0;
        if (ui->selected_link < 0) ui->selected_link = ui->rr.link_count - 1;

        /* Scroll to make link visible */
        Link *lk = &ui->rr.links[ui->selected_link];
        if (lk->line_start < ui->scroll)
            ui->scroll = lk->line_start;
        else if (lk->line_start >= ui->scroll + ui->content_rows)
            ui->scroll = lk->line_start - ui->content_rows + 1;
        clamp_scroll(ui);
    }
}

/* ---- Main loop ---- */

void ui_run(Book *book, TOCList *toc, ReadingState *rs, Config *cfg) {
    UIState ui = {0};
    ui.book = book;
    ui.toc = toc;
    ui.rs = rs;
    ui.cfg = cfg;
    ui.selected_link = -1;
    ui.running = 1;

    /* Init ncurses */
    initscr();
    raw();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    start_color();
    use_default_colors();
    theme_apply();

    /* Handle resize */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_resize;
    sigaction(SIGWINCH, &sa, NULL);

    getmaxyx(stdscr, ui.rows, ui.cols);
    ui.content_rows = ui.rows - 1;

    /* Load initial chapter */
    load_chapter(&ui);

    /* Restore scroll position */
    ui.scroll = rs->scroll_line;
    clamp_scroll(&ui);

    while (ui.running) {
        /* Handle terminal resize */
        if (got_resize) {
            got_resize = 0;
            endwin();
            refresh();
            getmaxyx(stdscr, ui.rows, ui.cols);
            ui.content_rows = ui.rows - 1;
            load_chapter(&ui); /* re-render for new width */
            clamp_scroll(&ui);
        }

        draw_screen(&ui);
        int ch = getch();
        if (ch == KEY_RESIZE) {
            got_resize = 1;
            continue;
        }

        Action action = map_key(&ui, ch);

        switch (action) {
            case ACTION_QUIT:
                ui.running = 0;
                break;

            case ACTION_SCROLL_DOWN:
                ui.scroll++;
                clamp_scroll(&ui);
                break;

            case ACTION_SCROLL_UP:
                ui.scroll--;
                clamp_scroll(&ui);
                break;

            case ACTION_PAGE_DOWN:
                ui.scroll += ui.content_rows;
                clamp_scroll(&ui);
                break;

            case ACTION_PAGE_UP:
                ui.scroll -= ui.content_rows;
                clamp_scroll(&ui);
                break;

            case ACTION_HALF_PAGE_DOWN:
                ui.scroll += ui.content_rows / 2;
                clamp_scroll(&ui);
                break;

            case ACTION_HALF_PAGE_UP:
                ui.scroll -= ui.content_rows / 2;
                clamp_scroll(&ui);
                break;

            case ACTION_NEXT_CHAPTER:
                goto_chapter(&ui, ui.rs->spine_index + 1);
                break;

            case ACTION_PREV_CHAPTER:
                goto_chapter(&ui, ui.rs->spine_index - 1);
                break;

            case ACTION_TOC: {
                int sel = show_toc_popup(&ui);
                if (sel >= 0 && sel < ui.toc->count) {
                    TOCEntry *e = &ui.toc->entries[sel];
                    if (e->spine_index >= 0) {
                        push_back(&ui);
                        goto_chapter(&ui, e->spine_index);
                        /* Try to jump to fragment */
                        const char *frag = href_get_fragment(e->href);
                        if (frag) {
                            int line = render_find_anchor(&ui.rr, frag);
                            if (line >= 0) {
                                ui.scroll = line;
                                clamp_scroll(&ui);
                            }
                        }
                    }
                }
                break;
            }

            case ACTION_SEARCH:
                prompt_search(&ui, 1);
                break;

            case ACTION_SEARCH_BACK:
                prompt_search(&ui, -1);
                break;

            case ACTION_SEARCH_NEXT:
                search_jump(&ui, ui.search_dir > 0 ? 1 : -1);
                break;

            case ACTION_SEARCH_PREV:
                search_jump(&ui, ui.search_dir > 0 ? -1 : 1);
                break;

            case ACTION_CYCLE_LINK_FWD:
                cycle_link(&ui, 1);
                break;

            case ACTION_CYCLE_LINK_BACK:
                cycle_link(&ui, -1);
                break;

            case ACTION_FOLLOW_LINK:
                follow_link(&ui);
                break;

            case ACTION_GO_BACK: {
                BackEntry be;
                if (pop_back(&ui, &be) == 0) {
                    if (be.spine_index != ui.rs->spine_index)
                        goto_chapter(&ui, be.spine_index);
                    ui.scroll = be.scroll_line;
                    clamp_scroll(&ui);
                }
                break;
            }

            case ACTION_THEME_SELECT: {
                int sel = show_theme_popup(&ui);
                if (sel >= 0) {
                    theme_select(sel);
                    theme_apply();
                }
                break;
            }

            case ACTION_HELP:
                show_help(&ui);
                break;

            case ACTION_NONE:
                break;
        }

        /* Update reading state */
        ui.rs->scroll_line = ui.scroll;
    }

    /* Cleanup */
    free(ui.search_hits);
    render_free(&ui.rr);
    endwin();
}
