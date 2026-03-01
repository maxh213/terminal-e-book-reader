#include "config.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_load(Config *cfg) {
    memset(cfg, 0, sizeof(*cfg));

    char *config_dir = xdg_config_home();
    char *dir = path_join(config_dir, "tread");
    char *path = path_join(dir, "tread.conf");
    free(dir);
    free(config_dir);

    FILE *f = fopen(path, "r");
    free(path);
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *s = str_trim(line);
        if (!*s || *s == '#' || *s == ';') continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';

        char *key = str_trim(s);
        char *val = str_trim(eq + 1);

        if (strcmp(key, "mode") == 0) {
            cfg->vi_mode = (strcmp(val, "vi") == 0);
        } else if (strcmp(key, "theme") == 0) {
            free(cfg->theme_name);
            cfg->theme_name = xstrdup(val);
        }
    }

    fclose(f);
}

void config_free(Config *cfg) {
    free(cfg->theme_name);
    cfg->theme_name = NULL;
}
