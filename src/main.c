#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <limits.h>
#include "epub.h"
#include "renderer.h"
#include "ui.h"
#include "toc.h"
#include "theme.h"
#include "state.h"
#include "config.h"
#include "util.h"

static void usage(void) {
    fprintf(stderr,
        "Usage: tread [OPTIONS] <file.epub>\n"
        "\n"
        "Options:\n"
        "  --vi           Use vi-style keybindings\n"
        "  --theme NAME   Use named theme\n"
        "  --no-restore   Don't restore previous reading position\n"
        "  -h, --help     Show this help\n"
        "  -v, --version  Show version\n"
    );
}

static void version(void) {
    printf("tread 0.1.0\n");
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");

    const char *epub_path = NULL;
    int vi_mode = 0;
    const char *theme_name = NULL;
    int no_restore = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--vi") == 0) {
            vi_mode = 1;
        } else if (strcmp(argv[i], "--theme") == 0) {
            if (++i >= argc) { fprintf(stderr, "tread: --theme requires argument\n"); return 1; }
            theme_name = argv[i];
        } else if (strcmp(argv[i], "--no-restore") == 0) {
            no_restore = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            version();
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "tread: unknown option '%s'\n", argv[i]);
            return 1;
        } else {
            epub_path = argv[i];
        }
    }

    if (!epub_path) {
        usage();
        return 1;
    }

    /* Resolve to absolute path for state persistence */
    char abs_path[PATH_MAX];
    if (!realpath(epub_path, abs_path)) {
        fprintf(stderr, "tread: cannot open '%s': ", epub_path);
        perror(NULL);
        return 1;
    }

    /* Load config file */
    Config cfg = {0};
    config_load(&cfg);
    if (vi_mode) cfg.vi_mode = 1;
    if (theme_name) {
        free(cfg.theme_name);
        cfg.theme_name = xstrdup(theme_name);
    }

    /* Open EPUB */
    Book book = {0};
    if (epub_open(&book, abs_path) != 0) {
        fprintf(stderr, "tread: failed to open '%s'\n", epub_path);
        config_free(&cfg);
        return 1;
    }

    /* Parse TOC */
    TOCList toc = {0};
    toc_parse(&toc, &book);

    /* Restore reading position */
    ReadingState rs = {0};
    if (!no_restore) {
        state_load(&rs, abs_path);
        if (rs.spine_index >= book.spine_count)
            rs.spine_index = 0;
    }

    /* Init themes */
    theme_init();
    if (cfg.theme_name)
        theme_select_by_name(cfg.theme_name);

    /* Run UI */
    ui_run(&book, &toc, &rs, &cfg);

    /* Save reading position */
    state_save(&rs, abs_path);

    /* Cleanup */
    toc_free(&toc);
    epub_close(&book);
    config_free(&cfg);

    return 0;
}
