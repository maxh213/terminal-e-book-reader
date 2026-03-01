#include "theme.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <dirent.h>

#define MAX_THEMES 32

static Theme themes[MAX_THEMES];
static int num_themes = 0;
static int current_theme = 0;

static void add_theme(const char *name,
                      short fg, short bg,
                      short sfg, short sbg,
                      short lfg, short lbg,
                      short hfg, short hbg,
                      short hlfg, short hlbg,
                      short tfg, short tbg,
                      short tsfg, short tsbg) {
    if (num_themes >= MAX_THEMES) return;
    Theme *t = &themes[num_themes++];
    t->name = xstrdup(name);
    t->fg = fg; t->bg = bg;
    t->status_fg = sfg; t->status_bg = sbg;
    t->link_fg = lfg; t->link_bg = lbg;
    t->heading_fg = hfg; t->heading_bg = hbg;
    t->highlight_fg = hlfg; t->highlight_bg = hlbg;
    t->toc_fg = tfg; t->toc_bg = tbg;
    t->toc_sel_fg = tsfg; t->toc_sel_bg = tsbg;
}

static void load_builtin_themes(void) {
    /*                name        fg  bg  sfg sbg lfg lbg hfg hbg hlfg hlbg tfg tbg tsfg tsbg */
    add_theme("default",          -1, -1,  0,  7,  4, -1,  3, -1,   0,   3, -1, -1,  0,   6);
    add_theme("dark",              7,  0,  0,  4,  6,  0,  3,  0,   0,   3,  7,  0,  0,   6);
    add_theme("light",             0,  7,  7,  4,  4,  7,  1,  7,   7,   3,  0,  7,  7,   4);
}

static short parse_color(const char *s) {
    if (!s || !*s) return -1;
    if (strcmp(s, "default") == 0 || strcmp(s, "-1") == 0) return -1;
    return (short)atoi(s);
}

static void load_theme_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;

    Theme t = {0};
    t.fg = t.bg = -1;
    t.status_fg = 0; t.status_bg = 7;
    t.link_fg = 4; t.link_bg = -1;
    t.heading_fg = 3; t.heading_bg = -1;
    t.highlight_fg = 0; t.highlight_bg = 3;
    t.toc_fg = -1; t.toc_bg = -1;
    t.toc_sel_fg = 0; t.toc_sel_bg = 6;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *s = str_trim(line);
        if (!*s || *s == '#' || *s == ';' || *s == '[') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = str_trim(s);
        char *val = str_trim(eq + 1);

        if (strcmp(key, "name") == 0)           { t.name = xstrdup(val); }
        else if (strcmp(key, "fg") == 0)         { t.fg = parse_color(val); }
        else if (strcmp(key, "bg") == 0)         { t.bg = parse_color(val); }
        else if (strcmp(key, "status_fg") == 0)  { t.status_fg = parse_color(val); }
        else if (strcmp(key, "status_bg") == 0)  { t.status_bg = parse_color(val); }
        else if (strcmp(key, "link_fg") == 0)    { t.link_fg = parse_color(val); }
        else if (strcmp(key, "link_bg") == 0)    { t.link_bg = parse_color(val); }
        else if (strcmp(key, "heading_fg") == 0) { t.heading_fg = parse_color(val); }
        else if (strcmp(key, "heading_bg") == 0) { t.heading_bg = parse_color(val); }
        else if (strcmp(key, "highlight_fg") == 0) { t.highlight_fg = parse_color(val); }
        else if (strcmp(key, "highlight_bg") == 0) { t.highlight_bg = parse_color(val); }
        else if (strcmp(key, "toc_fg") == 0)     { t.toc_fg = parse_color(val); }
        else if (strcmp(key, "toc_bg") == 0)     { t.toc_bg = parse_color(val); }
        else if (strcmp(key, "toc_sel_fg") == 0) { t.toc_sel_fg = parse_color(val); }
        else if (strcmp(key, "toc_sel_bg") == 0) { t.toc_sel_bg = parse_color(val); }
    }
    fclose(f);

    if (!t.name) {
        /* Derive name from filename */
        const char *base = path_basename(path);
        char *n = xstrdup(base);
        char *dot = strrchr(n, '.');
        if (dot) *dot = '\0';
        t.name = n;
    }

    if (num_themes < MAX_THEMES)
        themes[num_themes++] = t;
    else
        free(t.name);
}

static void load_user_themes(void) {
    char *config = xdg_config_home();
    char *dir = path_join(config, "tread/themes");
    free(config);

    DIR *d = opendir(dir);
    if (!d) { free(dir); return; }

    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (!str_endswith(ent->d_name, ".conf")) continue;
        char *path = path_join(dir, ent->d_name);
        load_theme_file(path);
        free(path);
    }
    closedir(d);
    free(dir);
}

void theme_init(void) {
    load_builtin_themes();
    load_user_themes();
}

Theme *theme_current(void) {
    return &themes[current_theme];
}

int theme_select_by_name(const char *name) {
    for (int i = 0; i < num_themes; i++) {
        if (strcmp(themes[i].name, name) == 0) {
            current_theme = i;
            return 0;
        }
    }
    return -1;
}

int theme_select(int index) {
    if (index < 0 || index >= num_themes) return -1;
    current_theme = index;
    return 0;
}

int theme_count(void) { return num_themes; }

const char *theme_name(int index) {
    if (index < 0 || index >= num_themes) return NULL;
    return themes[index].name;
}

void theme_apply(void) {
    if (!has_colors()) return;
    Theme *t = theme_current();
    use_default_colors();
    init_pair(PAIR_TEXT,      t->fg, t->bg);
    init_pair(PAIR_STATUS,    t->status_fg, t->status_bg);
    init_pair(PAIR_LINK,      t->link_fg, t->link_bg);
    init_pair(PAIR_HEADING,   t->heading_fg, t->heading_bg);
    init_pair(PAIR_HIGHLIGHT, t->highlight_fg, t->highlight_bg);
    init_pair(PAIR_TOC,       t->toc_fg, t->toc_bg);
    init_pair(PAIR_TOC_SEL,   t->toc_sel_fg, t->toc_sel_bg);
}

void theme_cleanup(void) {
    for (int i = 0; i < num_themes; i++)
        free(themes[i].name);
    num_themes = 0;
}
