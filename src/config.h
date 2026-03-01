#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    int vi_mode;
    char *theme_name;
} Config;

/* Load config from ~/.config/tread/tread.conf */
void config_load(Config *cfg);
void config_free(Config *cfg);

#endif
