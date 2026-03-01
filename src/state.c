#include "state.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static char *state_dir(void) {
    char *data = xdg_data_home();
    char *dir = path_join(data, "tread/state");
    free(data);
    return dir;
}

static char *state_path(const char *filepath) {
    unsigned int hash = fnv1a_hash(filepath);
    char name[32];
    snprintf(name, sizeof(name), "%08x.state", hash);
    char *dir = state_dir();
    char *path = path_join(dir, name);
    free(dir);
    return path;
}

static int mkdirp(const char *path) {
    char *tmp = xstrdup(path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                free(tmp);
                return -1;
            }
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
    free(tmp);
    return 0;
}

int state_load(ReadingState *rs, const char *filepath) {
    rs->spine_index = 0;
    rs->scroll_line = 0;

    char *path = state_path(filepath);
    FILE *f = fopen(path, "r");
    free(path);
    if (!f) return -1;

    char stored_path[4096] = {0};
    int spine = 0, scroll = 0;

    /* Format: filepath\nspine_index scroll_line */
    if (!fgets(stored_path, sizeof(stored_path), f)) {
        fclose(f);
        return -1;
    }
    /* Strip newline */
    size_t len = strlen(stored_path);
    if (len > 0 && stored_path[len - 1] == '\n')
        stored_path[len - 1] = '\0';

    /* Validate path matches (handle hash collisions) */
    if (strcmp(stored_path, filepath) != 0) {
        fclose(f);
        return -1;
    }

    if (fscanf(f, "%d %d", &spine, &scroll) == 2) {
        rs->spine_index = spine;
        rs->scroll_line = scroll;
    }

    fclose(f);
    return 0;
}

int state_save(ReadingState *rs, const char *filepath) {
    char *dir = state_dir();
    if (mkdirp(dir) != 0) {
        free(dir);
        return -1;
    }
    free(dir);

    char *path = state_path(filepath);
    FILE *f = fopen(path, "w");
    free(path);
    if (!f) return -1;

    fprintf(f, "%s\n%d %d\n", filepath, rs->spine_index, rs->scroll_line);
    fclose(f);
    return 0;
}
